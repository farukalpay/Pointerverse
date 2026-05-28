// SPDX-License-Identifier: Apache-2.0
#include "pv/core/world.hpp"

#include <fmt/format.h>
#include <stdexcept>
#include <utility>

namespace pv {
namespace {

std::string key(std::string_view value) {
    return std::string{value};
}

}  // namespace

World::World(std::string name, WorldId id) : id_(id), name_(std::move(name)) {
    if (name_.empty()) {
        name_ = "world";
    }
}

void World::reset(std::string name, WorldId id) {
    *this = World{std::move(name), id};
}

WorldId World::id() const noexcept {
    return id_;
}

Epoch World::epoch() const noexcept {
    return epoch_;
}

const std::string& World::name() const noexcept {
    return name_;
}

TypeId World::type_id(std::string_view name) {
    return types_.intern(name);
}

RelationType World::relation_type(std::string_view name) {
    return relations_.intern(name);
}

std::string World::type_name(TypeId type) const {
    return types_.name(type);
}

std::string World::relation_name(RelationType relation) const {
    return relations_.name(relation);
}

Delta World::object_delta(std::string name, std::string_view type) {
    Delta delta;
    delta.creates.push_back(ObjectCreate{std::move(name), type_id(type), ExistenceState::Alive});
    return delta;
}

Delta World::link_delta(ObjectId from, ObjectId to, std::string_view relation, double weight, CausalRole role) {
    Delta delta;
    delta.links.push_back(PointerCreate{from, to, relation_type(relation), role, Weight{weight}, "core"});
    return delta;
}

Delta World::existence_delta(ObjectId object, ExistenceState state) {
    Delta delta;
    delta.updates.push_back(ObjectUpdate{object, std::nullopt, state});
    return delta;
}

CommitResult World::commit(const Delta& delta, const Verifier& verifier) {
    CommitResult result;
    result.before_epoch = epoch_;

    World candidate = *this;
    try {
        (void)candidate.apply_delta_unchecked(delta);
    } catch (const std::exception& error) {
        append_rejection_trace(delta, error.what(), {});
        result.after_epoch = epoch_;
        result.accepted = false;
        result.world_hash = hash();
        return result;
    }

    const auto before = snapshot();
    const auto after = candidate.snapshot();
    auto verification = verifier.check(LawCheckContext{before, delta, after});
    result.law_statuses = verification.statuses;
    result.violations = verification.violations;

    if (!verification.accepted) {
        append_rejection_trace(delta, "transition rejected by active laws", verification.violations);
        result.after_epoch = epoch_;
        result.accepted = false;
        result.world_hash = hash();
        return result;
    }

    auto events = apply_delta_unchecked(delta);
    for (const auto& status : verification.statuses) {
        events.push_back(TraceEvent{
            epoch_,
            "law.check",
            {
                {"law", status.law},
                {"status", status.passed ? "ok" : to_string(status.severity)},
                {"detail", status.explanation}
            },
            {{"magnitude", status.magnitude}}
        });
    }
    trace_.append(events);

    result.accepted = true;
    result.after_epoch = epoch_;
    result.events = std::move(events);
    result.world_hash = hash();
    return result;
}

EvolveResult World::evolve(std::size_t steps, const Verifier& verifier) {
    EvolveResult result;
    result.requested_steps = steps;

    for (std::size_t index = 0; index < steps; ++index) {
        Delta delta;
        delta.events.push_back(TraceEvent{
            {},
            "world.evolve",
            {{"step", std::to_string(index + 1)}},
            {}
        });
        auto commit_result = commit(delta, verifier);
        result.last_law_statuses = commit_result.law_statuses;
        if (commit_result.accepted) {
            result.completed_steps += 1;
        } else {
            result.rejected_steps += 1;
        }
    }

    return result;
}

WorldSnapshot World::snapshot() const {
    WorldSnapshot out;
    out.world = id_;
    out.world_name = name_;
    out.epoch = epoch_;

    const auto& type_names = types_.names();
    for (std::size_t index = 0; index < type_names.size(); ++index) {
        out.type_names.emplace(static_cast<std::uint32_t>(index + 1), type_names[index]);
    }

    const auto& relation_names = relations_.names();
    for (std::size_t index = 0; index < relation_names.size(); ++index) {
        out.relation_names.emplace(static_cast<std::uint32_t>(index + 1), relation_names[index]);
    }

    out.objects.reserve(objects_.size());
    for (const auto& object_view : objects_.objects()) {
        ObjectSnapshot snapshot;
        snapshot.id = object_view.id;
        snapshot.name = object_view.name;
        snapshot.type = object_view.type;
        snapshot.existence = object_view.existence;
        for (const auto& pointer : pointers_) {
            if (!pointer.active_at(epoch_)) {
                continue;
            }
            if (pointer.from == object_view.id) {
                snapshot.outgoing_count += 1;
            }
            if (pointer.to == object_view.id) {
                snapshot.incoming_count += 1;
            }
        }
        out.objects.push_back(std::move(snapshot));
    }

    out.pointers.reserve(pointers_.size());
    for (const auto& pointer : pointers_) {
        out.pointers.push_back(PointerSnapshot{
            pointer.id,
            pointer.from,
            pointer.to,
            pointer.relation,
            pointer.causal_role,
            pointer.weight,
            pointer.born_at,
            pointer.expires_at,
            pointer.law_domain
        });
    }

    return out;
}

bool World::contains(ObjectId id) const noexcept {
    return objects_.contains(id);
}

bool World::has_object(std::string_view name) const {
    return object_names_.contains(key(name));
}

ObjectId World::object_by_name(std::string_view name) const {
    const auto iter = object_names_.find(key(name));
    if (iter == object_names_.end()) {
        throw std::out_of_range(fmt::format("unknown object '{}'", name));
    }
    return iter->second;
}

const Object& World::object(ObjectId id) const {
    return objects_.get(id);
}

const PointerEdge& World::pointer(PointerId id) const {
    const auto index = pointer_index(id);
    if (!index.has_value()) {
        throw std::out_of_range(fmt::format("unknown pointer {}", to_string(id)));
    }
    return pointers_[*index];
}

const std::vector<Object>& World::objects() const noexcept {
    return objects_.objects();
}

const std::vector<PointerEdge>& World::pointers() const noexcept {
    return pointers_;
}

const TraceRecorder& World::trace() const noexcept {
    return trace_;
}

std::uint64_t World::hash() const {
    return snapshot().structural_hash();
}

std::vector<TraceEvent> World::apply_delta_unchecked(const Delta& delta) {
    std::vector<TraceEvent> events;
    const Epoch next_epoch{epoch_.value + 1};

    for (const auto& create : delta.creates) {
        if (object_names_.contains(create.name)) {
            throw std::invalid_argument(fmt::format("object '{}' already exists", create.name));
        }
        const auto id = objects_.create(create.name, create.type, create.existence);
        object_names_.emplace(create.name, id);
        events.push_back(TraceEvent{
            next_epoch,
            "object.create",
            {
                {"object", create.name},
                {"id", to_string(id)},
                {"type", type_name(create.type)},
                {"existence", to_string(create.existence)}
            },
            {}
        });
    }

    for (const auto& update : delta.updates) {
        if (!objects_.contains(update.id)) {
            throw std::invalid_argument(fmt::format("cannot update unknown object {}", to_string(update.id)));
        }
        if (update.type.has_value()) {
            objects_.set_type(update.id, *update.type);
        }
        if (update.existence.has_value()) {
            objects_.set_existence(update.id, *update.existence);
        }
        const auto& object_view = objects_.get(update.id);
        events.push_back(TraceEvent{
            next_epoch,
            "object.update",
            {
                {"object", object_view.name},
                {"id", to_string(update.id)},
                {"type", type_name(object_view.type)},
                {"existence", to_string(object_view.existence)}
            },
            {}
        });
    }

    for (const auto& link : delta.links) {
        if (!objects_.contains(link.from) || !objects_.contains(link.to)) {
            throw std::invalid_argument("pointer create references unknown object");
        }
        if (!link.relation.valid()) {
            throw std::invalid_argument("pointer relation type must be valid");
        }
        PointerEdge edge;
        edge.id = PointerId{next_pointer_id_++};
        edge.from = link.from;
        edge.to = link.to;
        edge.relation = link.relation;
        edge.causal_role = link.causal_role;
        edge.weight = link.weight;
        edge.born_at = next_epoch;
        edge.law_domain = link.law_domain.empty() ? "core" : link.law_domain;
        pointers_.push_back(edge);

        events.push_back(TraceEvent{
            next_epoch,
            "pointer.create",
            {
                {"pointer", to_string(edge.id)},
                {"from", objects_.get(edge.from).name},
                {"to", objects_.get(edge.to).name},
                {"relation", relation_name(edge.relation)},
                {"role", to_string(edge.causal_role)},
                {"law_domain", edge.law_domain}
            },
            {{"weight", edge.weight.value}}
        });
    }

    for (const auto& unlink : delta.unlinks) {
        const auto index = pointer_index(unlink.id);
        if (!index.has_value()) {
            throw std::invalid_argument(fmt::format("cannot remove unknown pointer {}", to_string(unlink.id)));
        }
        pointers_[*index].expires_at = next_epoch;
        events.push_back(TraceEvent{
            next_epoch,
            "pointer.remove",
            {{"pointer", to_string(unlink.id)}},
            {}
        });
    }

    for (auto event : delta.events) {
        if (event.epoch.value == 0) {
            event.epoch = next_epoch;
        }
        events.push_back(std::move(event));
    }

    epoch_ = next_epoch;
    return events;
}

std::optional<std::size_t> World::pointer_index(PointerId id) const noexcept {
    for (std::size_t index = 0; index < pointers_.size(); ++index) {
        if (pointers_[index].id == id) {
            return index;
        }
    }
    return std::nullopt;
}

void World::append_rejection_trace(const Delta&, const std::string& reason, const std::vector<LawViolation>& violations) {
    TraceEvent event{
        epoch_,
        "world.transition.rejected",
        {{"reason", reason}},
        {{"violations", static_cast<double>(violations.size())}}
    };
    trace_.append(std::move(event));

    for (const auto& violation : violations) {
        trace_.append(TraceEvent{
            epoch_,
            "law.check",
            {
                {"law", violation.law},
                {"status", to_string(violation.severity)},
                {"detail", violation.explanation}
            },
            {{"magnitude", violation.magnitude}}
        });
    }
}

}  // namespace pv
