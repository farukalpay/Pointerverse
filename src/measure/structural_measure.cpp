// SPDX-License-Identifier: Apache-2.0
#include "pv/measure/structural_measure.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include "pv/core/delta.hpp"
#include "pv/measure/graph_functional.hpp"
#include "pv/measure/graph_view.hpp"
#include "pv/storage/object_codec.hpp"
#include "pv/storage/repository.hpp"

namespace pv {
namespace {

std::uint64_t saturating_add(std::uint64_t left, std::uint64_t right) noexcept {
    constexpr auto max = std::numeric_limits<std::uint64_t>::max();
    if (max - left < right) {
        return max;
    }
    return left + right;
}

std::uint64_t saturating_mul(std::uint64_t left, std::uint64_t right) noexcept {
    if (left == 0 || right == 0) {
        return 0;
    }
    constexpr auto max = std::numeric_limits<std::uint64_t>::max();
    if (left > max / right) {
        return max;
    }
    return left * right;
}

void sort_objects(std::vector<ObjectId>& objects) {
    std::ranges::sort(objects, [](ObjectId left, ObjectId right) {
        return left < right;
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

std::uint64_t compat_structural_projection(
    std::size_t touched_objects,
    const std::vector<std::pair<std::string_view, FunctionalResult>>& results) {
    std::uint64_t value = touched_objects;
    for (const auto& [name, result] : results) {
        if (name == "forward_cone_mass") {
            value = saturating_add(value, result.value);
        } else if (name == "reverse_dependency_mass") {
            value = saturating_add(value, result.value / 2U);
        } else if (name == "propagated_mass") {
            value = saturating_add(value, result.value);
        } else if (name == "cut_vertex_impact") {
            value = saturating_add(value, saturating_mul(result.value, 2U));
        } else if (name == "boundary_expansion") {
            value = saturating_add(value, result.value);
        } else if (name == "path_multiplicity") {
            value = saturating_add(value, result.value / 4U);
        }
    }
    return value;
}

void append_witness(RiskEvidence& evidence, const FunctionalResult& result) {
    evidence.objects.insert(evidence.objects.end(), result.witness_objects.begin(), result.witness_objects.end());
    evidence.pointers.insert(evidence.pointers.end(), result.witness_pointers.begin(), result.witness_pointers.end());
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
    const auto graph = weighted_graph_view_for_commit(repository, commit);

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

    ForwardConeMass forward;
    ReverseDependencyMass reverse;
    PropagatedMass propagated;
    CutVertexImpact cut;
    BoundaryExpansion boundary;
    PathMultiplicity paths;
    const std::array<const GraphFunctional*, 6> functionals{
        &forward,
        &reverse,
        &propagated,
        &cut,
        &boundary,
        &paths
    };

    std::vector<std::pair<std::string_view, FunctionalResult>> results;
    results.reserve(functionals.size());
    for (const auto* functional : functionals) {
        results.emplace_back(functional->name(), functional->evaluate(graph, touched_objects));
    }

    MeasuredComponent component;
    component.name = "structural";
    component.value = compat_structural_projection(touched_objects.size(), results);
    component.evidence.component = component.name;
    component.evidence.input_root = record.before_root;
    component.evidence.output_root = record.after_root;
    component.evidence.objects = touched_objects;
    component.evidence.pointers = touched_pointers;
    component.evidence.commits.push_back(commit);
    for (const auto& [_, result] : results) {
        append_witness(component.evidence, result);
    }
    sort_objects(component.evidence.objects);
    sort_pointers(component.evidence.pointers);

    std::ostringstream explanation;
    explanation << "graph functional structural compat projection"
                << "; changed objects: " << touched_objects.size();
    for (const auto& [name, result] : results) {
        explanation << "; " << name << "=" << result.value
                    << " [" << result.explanation << "]";
    }
    component.evidence.explanation = explanation.str();
    return component;
}

}  // namespace pv
