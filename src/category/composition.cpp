// SPDX-License-Identifier: Apache-2.0
#include "pv/category/composition.hpp"

#include <algorithm>
#include <fmt/format.h>
#include <memory>
#include <optional>
#include <utility>

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

RelationType intern_relation_delta(Delta& delta, const WorldSnapshot& snapshot, std::string name) {
    if (const auto existing = find_relation(snapshot, name); existing.has_value()) {
        return *existing;
    }

    std::uint32_t next = 1;
    for (const auto& [id, _] : snapshot.relation_names) {
        next = std::max(next, id + 1);
    }
    const auto relation = RelationType{next};
    delta.append_intern_relation(std::move(name), relation);
    return relation;
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
    std::optional<RelationType> relation;
    std::size_t transformed = 0;
    for (const auto object : selection.objects) {
        const auto* view = snapshot.object(object);
        if (view == nullptr || view->type != signature_.domain) {
            continue;
        }
        if (!relation.has_value()) {
            relation = intern_relation_delta(delta, snapshot, fmt::format("morphism.{}", name_));
        }
        expire_prior_morphism_edges(delta, snapshot, object, *relation);
        if (signature_.domain != signature_.codomain) {
            delta.append_update(ObjectUpdate{ObjectRef{object}, signature_.codomain, std::nullopt});
        }
        delta.append_link(PointerCreate{
            ObjectRef{object},
            ObjectRef{object},
            *relation,
            CausalRole::Transformative,
            Weight{1.0},
            "morphism",
            {}
        });
        transformed += 1;
    }
    delta.append_event(TraceEvent{
        {},
        "morphism.apply",
        {{"name", name_}},
        {
            {"selected_objects", static_cast<double>(selection.objects.size())},
            {"transformed_objects", static_cast<double>(transformed)}
        }
    });
    return delta;
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
