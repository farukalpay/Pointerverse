// SPDX-License-Identifier: Apache-2.0
#include "pv/category/composition.hpp"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <variant>

#include "pv/core/value.hpp"

namespace pv {
namespace {

Selection project_selection(const Selection& selection, const WorldSnapshot& snapshot) {
    Selection out;
    for (const auto object : selection.objects) {
        if (snapshot.contains(object)) {
            out.objects.push_back(object);
        }
    }
    for (const auto pointer : selection.pointers) {
        if (snapshot.pointer(pointer) != nullptr) {
            out.pointers.push_back(pointer);
        }
    }
    return out;
}

Delta composition_failure(std::string_view name, std::string reason) {
    Delta delta;
    delta.append_event(TraceEvent{
        {},
        "morphism.compose.rejected",
        {{"name", std::string{name}}, {"reason", std::move(reason)}},
        {}
    });
    return delta;
}

bool active_at(const PointerSnapshot& pointer, Epoch epoch) noexcept {
    return pointer.born_at <= epoch && (!pointer.expires_at.has_value() || epoch < *pointer.expires_at);
}

std::optional<RelationType> find_relation(const WorldSnapshot& snapshot, std::string_view name) {
    for (const auto& [id, candidate] : snapshot.relation_names) {
        if (candidate == name) {
            return RelationType{id};
        }
    }
    return std::nullopt;
}

// Resolve relation names to ids, allocating new ids for unseen names and tracking
// allocations so several new relations interned into one delta never collide.
class RelationInterner {
public:
    RelationInterner(const WorldSnapshot& snapshot, Delta& delta) : snapshot_(snapshot), delta_(delta) {
        for (const auto& [id, name] : snapshot_.relation_names) {
            next_ = std::max(next_, id + 1);
        }
    }

    RelationType intern(const std::string& name) {
        if (const auto existing = find_relation(snapshot_, name); existing.has_value()) {
            return *existing;
        }
        if (const auto iter = cache_.find(name); iter != cache_.end()) {
            return iter->second;
        }
        const RelationType relation{next_++};
        delta_.append_intern_relation(name, relation);
        cache_.emplace(name, relation);
        return relation;
    }

private:
    const WorldSnapshot& snapshot_;
    Delta& delta_;
    std::unordered_map<std::string, RelationType> cache_;
    std::uint32_t next_{1};
};

std::optional<ObjectId> object_id_by_name(const WorldSnapshot& snapshot, std::string_view name) {
    for (const auto& object : snapshot.objects) {
        if (object.name == name) {
            return object.id;
        }
    }
    return std::nullopt;
}

std::optional<double> numeric_value(const Value& value) {
    switch (value.kind) {
    case ValueKind::Int64:
        return static_cast<double>(std::get<std::int64_t>(value.data));
    case ValueKind::UInt64:
        return static_cast<double>(std::get<std::uint64_t>(value.data));
    case ValueKind::Float64:
        return std::get<double>(value.data);
    default:
        return std::nullopt;
    }
}

std::optional<double> attribute_number(const ObjectSnapshot& object, const std::string& key) {
    for (const auto& attribute : object.attributes) {
        if (attribute.key == key) {
            return numeric_value(attribute.value);
        }
    }
    return std::nullopt;
}

// Store an integral result as an unsigned/signed integer so attributes read back
// cleanly (generation = 2, not 2.0); fall back to float for fractional results.
Value number_value(double result) {
    if (std::isfinite(result) && std::floor(result) == result) {
        if (result >= 0.0) {
            return uint64_value(static_cast<std::uint64_t>(result));
        }
        return int64_value(static_cast<std::int64_t>(result));
    }
    return float64_value(result);
}

// Evaluate a morphism `set` expression against the object's current attributes.
// Returns nullopt if any operand attribute is missing or non-numeric, so the
// caller can skip the action instead of writing a bogus value.
std::optional<double> evaluate_expr(const std::vector<MorphismExprTerm>& expr, const ObjectSnapshot& object) {
    if (expr.empty()) {
        return std::nullopt;
    }
    const auto operand = [&](const MorphismExprTerm& term) -> std::optional<double> {
        return term.literal ? std::optional<double>{term.value} : attribute_number(object, term.attribute);
    };
    auto accumulator = operand(expr.front());
    if (!accumulator.has_value()) {
        return std::nullopt;
    }
    for (std::size_t index = 1; index < expr.size(); ++index) {
        const auto rhs = operand(expr[index]);
        if (!rhs.has_value()) {
            return std::nullopt;
        }
        switch (expr[index].op) {
        case '+': *accumulator += *rhs; break;
        case '-': *accumulator -= *rhs; break;
        case '*': *accumulator *= *rhs; break;
        case '/':
            if (*rhs == 0.0) {
                return std::nullopt;
            }
            *accumulator /= *rhs;
            break;
        default:
            return std::nullopt;
        }
    }
    return accumulator;
}

void expire_prior_morphism_edges(Delta& delta, const WorldSnapshot& snapshot, ObjectId object, RelationType relation) {
    for (const auto& pointer : snapshot.pointers) {
        if (pointer.from == object
            && pointer.to == object
            && pointer.relation == relation
            && pointer.law_domain == "morphism"
            && active_at(pointer, snapshot.epoch)) {
            delta.append_unlink(PointerRemove{pointer.id});
        }
    }
}

}  // namespace

IdentityMorphism::IdentityMorphism(TypeId type) : type_(type) {}

std::string_view IdentityMorphism::name() const {
    return "id";
}

MorphismSignature IdentityMorphism::signature() const {
    return MorphismSignature{type_, type_};
}

Delta IdentityMorphism::apply(const WorldSnapshot&, const Selection&) const {
    Delta delta;
    delta.append_event(TraceEvent{
        {},
        "morphism.apply",
        {{"name", "id"}, {"kind", "identity"}},
        {{"delta_size", 0.0}}
    });
    return delta;
}

DefinedMorphism::DefinedMorphism(std::string name, MorphismSignature signature)
    : name_(std::move(name)), signature_(signature) {}

std::string_view DefinedMorphism::name() const {
    return name_;
}

MorphismSignature DefinedMorphism::signature() const {
    return signature_;
}

Delta DefinedMorphism::apply(const WorldSnapshot& snapshot, const Selection& selection) const {
    Delta delta;
    RelationInterner interner{snapshot, delta};
    std::optional<RelationType> self_relation;
    std::size_t transformed = 0;
    std::size_t applied_actions = 0;
    std::size_t skipped_actions = 0;
    for (const auto object : selection.objects) {
        const auto* view = snapshot.object(object);
        if (view == nullptr || view->type != signature_.domain) {
            continue;
        }
        if (!self_relation.has_value()) {
            self_relation = interner.intern(fmt::format("morphism.{}", name_));
        }
        expire_prior_morphism_edges(delta, snapshot, object, *self_relation);
        if (signature_.domain != signature_.codomain) {
            delta.append_update(ObjectUpdate{ObjectRef{object}, signature_.codomain, std::nullopt});
        }
        delta.append_link(PointerCreate{
            ObjectRef{object},
            ObjectRef{object},
            *self_relation,
            CausalRole::Transformative,
            Weight{1.0},
            "morphism",
            {}
        });

        // Beyond retyping: run the declared transformation actions on the object.
        for (const auto& action : actions_) {
            if (const auto* set = std::get_if<MorphismSetAttribute>(&action)) {
                if (const auto result = evaluate_expr(set->expr, *view); result.has_value()) {
                    delta.append_set_object_attribute(ObjectRef{object}, Attribute{set->key, number_value(*result)});
                    applied_actions += 1;
                } else {
                    skipped_actions += 1;
                }
            } else if (const auto* emit = std::get_if<MorphismEmitEdge>(&action)) {
                const auto target = object_id_by_name(snapshot, emit->target);
                if (target.has_value()) {
                    const auto relation = interner.intern(emit->relation);
                    delta.append_link(PointerCreate{
                        ObjectRef{emit->reverse ? *target : object},
                        ObjectRef{emit->reverse ? object : *target},
                        relation,
                        emit->role,
                        Weight{emit->weight},
                        "morphism",
                        {}
                    });
                    applied_actions += 1;
                } else {
                    skipped_actions += 1;
                }
            }
        }
        transformed += 1;
    }
    delta.append_event(TraceEvent{
        {},
        "morphism.apply",
        {{"name", name_}},
        {
            {"selected_objects", static_cast<double>(selection.objects.size())},
            {"transformed_objects", static_cast<double>(transformed)},
            {"applied_actions", static_cast<double>(applied_actions)},
            {"skipped_actions", static_cast<double>(skipped_actions)}
        }
    });
    return delta;
}

void DefinedMorphism::add_action(MorphismAction action) {
    actions_.push_back(std::move(action));
}

const std::vector<MorphismAction>& DefinedMorphism::actions() const noexcept {
    return actions_;
}

ComposedMorphism::ComposedMorphism(
    std::string name,
    MorphismSignature signature,
    std::shared_ptr<const Morphism> first,
    std::shared_ptr<const Morphism> second)
    : name_(std::move(name)),
      signature_(signature),
      first_(std::move(first)),
      second_(std::move(second)) {}

std::string_view ComposedMorphism::name() const {
    return name_;
}

MorphismSignature ComposedMorphism::signature() const {
    return signature_;
}

Delta ComposedMorphism::apply(const WorldSnapshot& snapshot, const Selection& selection) const {
    if (!first_ || !second_) {
        return composition_failure(name_, "composition has a null child morphism");
    }

    const auto first_delta = first_->apply(snapshot, selection);
    const auto mid = SnapshotOverlay{snapshot}.apply(first_delta);
    if (!mid.has_value()) {
        return composition_failure(name_, fmt::format("first morphism overlay failed: {}", to_string(mid.error())));
    }

    const auto second_delta = second_->apply(*mid, project_selection(selection, *mid));
    auto merged = merge_sequential(snapshot, first_delta, second_delta);
    if (!merged.has_value()) {
        return composition_failure(name_, fmt::format("sequential merge failed: {}", to_string(merged.error())));
    }

    merged->append_event(TraceEvent{
        {},
        "morphism.compose",
        {{"name", name_}, {"kind", "composition"}},
        {{"selected_objects", static_cast<double>(selection.objects.size())}}
    });
    return *merged;
}

std::expected<std::shared_ptr<const Morphism>, CompositionError>
compose(std::shared_ptr<const Morphism> g, std::shared_ptr<const Morphism> f) {
    if (!g || !f) {
        return std::unexpected(CompositionError::BrokenInvariant);
    }

    const auto g_signature = g->signature();
    const auto f_signature = f->signature();
    if (f_signature.codomain != g_signature.domain) {
        return std::unexpected(CompositionError::DomainCodomainMismatch);
    }
    return std::make_shared<ComposedMorphism>(
        fmt::format("{} after {}", g->name(), f->name()),
        MorphismSignature{f_signature.domain, g_signature.codomain},
        std::move(f),
        std::move(g));
}

std::string to_string(CompositionError error) {
    switch (error) {
    case CompositionError::DomainCodomainMismatch:
        return "DomainCodomainMismatch";
    case CompositionError::LawConflict:
        return "LawConflict";
    case CompositionError::ObserverLeak:
        return "ObserverLeak";
    case CompositionError::NonDeterministicBoundary:
        return "NonDeterministicBoundary";
    case CompositionError::InvalidPartiality:
        return "InvalidPartiality";
    case CompositionError::BrokenInvariant:
        return "BrokenInvariant";
    }
    return "BrokenInvariant";
}

}  // namespace pv
