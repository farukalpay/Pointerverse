// SPDX-License-Identifier: Apache-2.0
#include "pv/measure/structural_measure.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include "pv/core/delta.hpp"
#include "pv/core/world_index.hpp"
#include "pv/storage/object_codec.hpp"
#include "pv/storage/repository.hpp"

namespace pv {
namespace {

using ObjectKey = std::pair<ObjectIndex, Generation>;

ObjectKey key(ObjectId id) noexcept {
    return {id.index, id.generation};
}

std::uint64_t saturating_add(std::uint64_t left, std::uint64_t right) noexcept {
    constexpr auto max = std::numeric_limits<std::uint64_t>::max();
    if (max - left < right) {
        return max;
    }
    return left + right;
}

std::uint64_t weight_scaled(Weight weight) noexcept {
    if (!std::isfinite(weight.value) || weight.value <= 0.0) {
        return 0;
    }
    constexpr double scale = 1000000.0;
    if (weight.value >= static_cast<double>(std::numeric_limits<std::uint64_t>::max()) / scale) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return static_cast<std::uint64_t>(std::llround(weight.value * scale));
}

bool active_at(const PointerSnapshot& pointer, Epoch epoch) noexcept {
    return pointer.born_at <= epoch && (!pointer.expires_at.has_value() || epoch < *pointer.expires_at);
}

void sort_objects(std::vector<ObjectId>& objects) {
    std::ranges::sort(objects, [](ObjectId left, ObjectId right) {
        if (left.index != right.index) {
            return left.index < right.index;
        }
        return left.generation < right.generation;
    });
    objects.erase(std::ranges::unique(objects).begin(), objects.end());
}

void sort_pointers(std::vector<PointerId>& pointers) {
    std::ranges::sort(pointers, {}, &PointerId::value);
    pointers.erase(std::ranges::unique(pointers).begin(), pointers.end());
}

std::optional<ObjectId> object_named(const WorldSnapshot& snapshot, std::string_view name) {
    for (const auto& object : snapshot.objects) {
        if (object.name == name || to_string(object.id) == name) {
            return object.id;
        }
    }
    return std::nullopt;
}

WorldGraphIndexEntry graph_entry_from_snapshot(CommitId commit, Hash256 snapshot_root, const WorldSnapshot& snapshot) {
    WorldIndex runtime_index;
    runtime_index.rebuild(snapshot);

    WorldGraphIndexEntry entry;
    entry.commit = commit;
    entry.snapshot = snapshot_root;
    entry.names.reserve(snapshot.objects.size());
    entry.adjacency.reserve(snapshot.objects.size());
    for (const auto& object : snapshot.objects) {
        entry.names.push_back(WorldObjectNameIndexEntry{object.name, object.id});
        entry.adjacency.push_back(WorldGraphAdjacencyEntry{
            object.id,
            {runtime_index.outgoing(object.id).begin(), runtime_index.outgoing(object.id).end()},
            {runtime_index.incoming(object.id).begin(), runtime_index.incoming(object.id).end()}
        });
    }
    for (const auto& [id, name] : snapshot.type_names) {
        const auto span = runtime_index.objects_by_type(TypeId{id});
        if (!span.empty()) {
            entry.types.push_back(WorldTypeIndexEntry{name, {span.begin(), span.end()}});
        }
    }
    for (const auto& [id, name] : snapshot.relation_names) {
        const auto span = runtime_index.relation(RelationType{id});
        if (!span.empty()) {
            entry.relations.push_back(WorldRelationIndexEntry{name, {span.begin(), span.end()}});
        }
    }
    for (const auto& pointer : snapshot.pointers) {
        if (!active_at(pointer, snapshot.epoch)) {
            continue;
        }
        entry.pointers.push_back(WorldGraphPointerEntry{
            pointer.id,
            pointer.from,
            pointer.to,
            snapshot.relation_name(pointer.relation),
            weight_scaled(pointer.weight)
        });
    }
    return entry;
}

const WorldGraphAdjacencyEntry* adjacency_for(const WorldGraphIndexEntry& graph, ObjectId object) {
    for (const auto& adjacency : graph.adjacency) {
        if (adjacency.object == object) {
            return &adjacency;
        }
    }
    return nullptr;
}

const WorldGraphPointerEntry* pointer_for(const WorldGraphIndexEntry& graph, PointerId pointer) {
    for (const auto& candidate : graph.pointers) {
        if (candidate.pointer == pointer) {
            return &candidate;
        }
    }
    return nullptr;
}

void add_event_touches(const CommitRecord& record, const WorldSnapshot& after, std::vector<ObjectId>& objects) {
    for (const auto& event : record.events) {
        for (const auto* field : {"object", "from", "to", "actor", "id"}) {
            const auto iter = event.fields.find(field);
            if (iter == event.fields.end() || iter->second.empty()) {
                continue;
            }
            if (const auto object = object_named(after, iter->second); object.has_value()) {
                objects.push_back(*object);
            }
        }
    }
}

WorldSnapshot before_snapshot_for(const Repository& repository, const CommitRecord& record, const WorldSnapshot& after) {
    if (record.parent.has_value()) {
        return repository.backend().snapshot(*record.parent);
    }
    WorldSnapshot before;
    before.world = record.world;
    before.world_name = after.world_name;
    before.epoch = record.before_epoch;
    return before;
}

std::vector<ObjectId> touched_objects_from_delta(
    const Delta& delta,
    const WorldSnapshot& before,
    const WorldSnapshot& after) {
    std::vector<ObjectId> touched;
    std::map<std::uint32_t, ObjectId> temps;

    auto resolve = [&](const ObjectRef& ref) -> std::optional<ObjectId> {
        if (const auto* object = std::get_if<ObjectId>(&ref)) {
            if (before.contains(*object) || after.contains(*object)) {
                return *object;
            }
            return std::nullopt;
        }
        const auto temp = std::get<TempObjectId>(ref);
        const auto iter = temps.find(temp.value);
        if (iter == temps.end()) {
            return std::nullopt;
        }
        return iter->second;
    };

    for (const auto& op : delta.ops) {
        switch (op.kind) {
        case OperationKind::CreateObject: {
            const auto& body = std::get<CreateObjectOp>(op.body);
            if (const auto object = object_named(after, body.name); object.has_value()) {
                temps.emplace(body.temp_id.value, *object);
                touched.push_back(*object);
            }
            break;
        }
        case OperationKind::SetObjectType:
            if (const auto object = resolve(std::get<SetObjectTypeOp>(op.body).object); object.has_value()) {
                touched.push_back(*object);
            }
            break;
        case OperationKind::SetObjectExistence:
            if (const auto object = resolve(std::get<SetObjectExistenceOp>(op.body).object); object.has_value()) {
                touched.push_back(*object);
            }
            break;
        case OperationKind::SetObjectAttribute:
            if (const auto object = resolve(std::get<SetObjectAttributeOp>(op.body).object); object.has_value()) {
                touched.push_back(*object);
            }
            break;
        case OperationKind::RemoveObjectAttribute:
            if (const auto object = resolve(std::get<RemoveObjectAttributeOp>(op.body).object); object.has_value()) {
                touched.push_back(*object);
            }
            break;
        case OperationKind::CreatePointer: {
            const auto& body = std::get<CreatePointerOp>(op.body);
            if (const auto object = resolve(body.from); object.has_value()) {
                touched.push_back(*object);
            }
            if (const auto object = resolve(body.to); object.has_value()) {
                touched.push_back(*object);
            }
            break;
        }
        case OperationKind::AssertObject:
            if (const auto object = resolve(std::get<AssertObjectOp>(op.body).object); object.has_value()) {
                touched.push_back(*object);
            }
            break;
        case OperationKind::ExpirePointer:
        case OperationKind::SetPointerWeight:
        case OperationKind::SetPointerAttribute:
        case OperationKind::RemovePointerAttribute:
        case OperationKind::EmitEvent:
        case OperationKind::InternType:
        case OperationKind::InternRelation:
        case OperationKind::AssertPointer:
        case OperationKind::AssertFact:
            break;
        }
    }

    sort_objects(touched);
    return touched;
}

std::vector<PointerId> touched_pointers_from_delta(const Delta& delta) {
    std::vector<PointerId> touched;
    for (const auto& op : delta.ops) {
        switch (op.kind) {
        case OperationKind::ExpirePointer:
            touched.push_back(std::get<ExpirePointerOp>(op.body).id);
            break;
        case OperationKind::SetPointerWeight:
            touched.push_back(std::get<SetPointerWeightOp>(op.body).id);
            break;
        case OperationKind::SetPointerAttribute:
            touched.push_back(std::get<SetPointerAttributeOp>(op.body).id);
            break;
        case OperationKind::RemovePointerAttribute:
            touched.push_back(std::get<RemovePointerAttributeOp>(op.body).id);
            break;
        case OperationKind::CreateObject:
        case OperationKind::SetObjectType:
        case OperationKind::SetObjectExistence:
        case OperationKind::SetObjectAttribute:
        case OperationKind::RemoveObjectAttribute:
        case OperationKind::CreatePointer:
        case OperationKind::EmitEvent:
        case OperationKind::InternType:
        case OperationKind::InternRelation:
        case OperationKind::AssertObject:
        case OperationKind::AssertPointer:
        case OperationKind::AssertFact:
            break;
        }
    }
    sort_pointers(touched);
    return touched;
}

}  // namespace

MeasuredComponent StructuralRiskMeasure::measure(
    const Repository& repository,
    std::string_view,
    CommitId commit) const {
    const auto record = repository.backend().commit_record(commit);
    const auto stored = repository.backend().stored_commit(commit);
    const auto after = repository.backend().snapshot(commit);
    const auto before = before_snapshot_for(repository, record, after);
    const auto delta = repository.objects().get_canonical<Delta>(stored.delta_object);

    std::string index_source = "persistent_commit_index";
    auto graph = repository.backend().world_index().find_commit(commit);
    if (!graph.has_value() || graph->snapshot != record.after_root) {
        index_source = "materialized_commit_snapshot";
        graph = graph_entry_from_snapshot(commit, record.after_root, after);
    }

    auto touched_objects = touched_objects_from_delta(delta, before, after);
    add_event_touches(record, after, touched_objects);
    for (const auto& violation : record.violations) {
        touched_objects.insert(touched_objects.end(), violation.objects.begin(), violation.objects.end());
    }
    sort_objects(touched_objects);

    auto touched_pointers = touched_pointers_from_delta(delta);
    for (const auto& violation : record.violations) {
        touched_pointers.insert(touched_pointers.end(), violation.pointers.begin(), violation.pointers.end());
    }
    sort_pointers(touched_pointers);

    std::set<ObjectKey> visited_objects;
    std::set<std::uint64_t> visited_pointers;
    std::vector<ObjectId> frontier = touched_objects;
    std::uint64_t weighted_mass = 0;
    std::uint64_t relation_fanout = 0;
    std::uint64_t articulation = 0;

    for (const auto object : touched_objects) {
        if (const auto* adjacency = adjacency_for(*graph, object); adjacency != nullptr) {
            relation_fanout = saturating_add(relation_fanout, adjacency->outgoing.size());
            if (!adjacency->incoming.empty() && !adjacency->outgoing.empty()) {
                articulation = saturating_add(articulation, 1);
            }
        }
        visited_objects.insert(key(object));
    }

    while (!frontier.empty()) {
        std::vector<ObjectId> next;
        for (const auto object : frontier) {
            const auto* adjacency = adjacency_for(*graph, object);
            if (adjacency == nullptr) {
                continue;
            }
            for (const auto pointer_id : adjacency->outgoing) {
                const auto* pointer = pointer_for(*graph, pointer_id);
                if (pointer == nullptr) {
                    continue;
                }
                if (visited_pointers.insert(pointer_id.value).second) {
                    weighted_mass = saturating_add(weighted_mass, pointer->weight_scaled);
                }
                if (visited_objects.insert(key(pointer->to)).second) {
                    next.push_back(pointer->to);
                }
            }
        }
        sort_objects(next);
        frontier = std::move(next);
    }

    const auto weighted_units = weighted_mass / 1000000ULL;
    std::uint64_t value = touched_objects.size();
    value = saturating_add(value, visited_objects.size());
    value = saturating_add(value, visited_pointers.size());
    value = saturating_add(value, weighted_units);
    value = saturating_add(value, articulation * 2U);
    value = saturating_add(value, relation_fanout);

    MeasuredComponent component;
    component.name = "structural";
    component.value = value;
    component.evidence.component = component.name;
    component.evidence.input_root = record.before_root;
    component.evidence.output_root = record.after_root;
    component.evidence.objects = touched_objects;
    component.evidence.pointers = touched_pointers;
    component.evidence.commits.push_back(commit);
    std::ostringstream explanation;
    explanation << "index source: " << index_source
                << "; changed objects: " << touched_objects.size()
                << "; forward cone objects: " << visited_objects.size()
                << "; forward cone pointers: " << visited_pointers.size()
                << "; weighted causal cone mass: " << weighted_units
                << "; touched articulation objects: " << articulation
                << "; relation fanout: " << relation_fanout;
    component.evidence.explanation = explanation.str();
    return component;
}

}  // namespace pv
