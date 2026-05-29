// SPDX-License-Identifier: Apache-2.0
#include "pv/runtime/merge.hpp"

#include <cmath>
#include <map>
#include <optional>
#include <set>

#include "pv/runtime/commit_graph.hpp"
#include "pv/runtime/world_store.hpp"

namespace pv {
namespace {

std::map<std::pair<ObjectIndex, Generation>, const ObjectSnapshot*> object_map(const WorldSnapshot& snapshot) {
    std::map<std::pair<ObjectIndex, Generation>, const ObjectSnapshot*> out;
    for (const auto& object : snapshot.objects) {
        out.emplace(std::pair{object.id.index, object.id.generation}, &object);
    }
    return out;
}

std::map<std::uint64_t, const PointerSnapshot*> pointer_map(const WorldSnapshot& snapshot) {
    std::map<std::uint64_t, const PointerSnapshot*> out;
    for (const auto& pointer : snapshot.pointers) {
        out.emplace(pointer.id.value, &pointer);
    }
    return out;
}

std::optional<TypeId> type_of(const ObjectSnapshot* object) {
    if (object == nullptr) {
        return std::nullopt;
    }
    return object->type;
}

std::optional<std::string> type_name_of(const WorldSnapshot& snapshot, const ObjectSnapshot* object) {
    if (object == nullptr) {
        return std::nullopt;
    }
    return snapshot.type_name(object->type);
}

std::optional<ExistenceState> existence_of(const ObjectSnapshot* object) {
    if (object == nullptr) {
        return std::nullopt;
    }
    return object->existence;
}

std::string name_of(const ObjectSnapshot* ancestor, const ObjectSnapshot* left, const ObjectSnapshot* right) {
    if (left != nullptr) {
        return left->name;
    }
    if (right != nullptr) {
        return right->name;
    }
    if (ancestor != nullptr) {
        return ancestor->name;
    }
    return {};
}

bool same_optional_epoch(const std::optional<Epoch>& left, const std::optional<Epoch>& right) {
    return left == right;
}

bool same_pointer(const PointerSnapshot* left, const PointerSnapshot* right) {
    if (left == nullptr || right == nullptr) {
        return left == right;
    }
    return left->from == right->from
        && left->to == right->to
        && left->relation == right->relation
        && left->causal_role == right->causal_role
        && canonical_f64(left->weight.value) == canonical_f64(right->weight.value)
        && left->born_at == right->born_at
        && same_optional_epoch(left->expires_at, right->expires_at)
        && left->law_domain == right->law_domain;
}

bool changed_from_ancestor(const PointerSnapshot* ancestor, const PointerSnapshot* value) {
    return !same_pointer(ancestor, value);
}

}  // namespace

MergeAnalysis analyze_merge(const WorldStore& store, BranchId left_id, BranchId right_id) {
    MergeAnalysis analysis;
    analysis.left = left_id;
    analysis.right = right_id;

    const auto& left_branch = store.branch(left_id);
    const auto& right_branch = store.branch(right_id);
    if (!left_branch.head.has_value() || !right_branch.head.has_value()) {
        analysis.status = MergeStatus::DivergentHistory;
        return analysis;
    }

    analysis.common_ancestor = store.graph().common_ancestor(*left_branch.head, *right_branch.head);
    if (!analysis.common_ancestor.has_value()) {
        analysis.status = MergeStatus::DivergentHistory;
        return analysis;
    }

    // Name the first commit each branch added after the fork point. This is the
    // first causally relevant commit on each side: the change that started the
    // divergence the rest of the analysis measures.
    const auto first_after_ancestor = [&](CommitId head) -> DivergencePoint {
        DivergencePoint point;
        if (head == *analysis.common_ancestor) {
            return point;
        }
        std::optional<CommitId> previous;
        for (const auto& id : store.graph().path_to_root(head)) {
            if (id == *analysis.common_ancestor) {
                point.commit = previous;
                break;
            }
            previous = id;
        }
        if (point.commit.has_value()) {
            if (const auto* record = store.commit_record(*point.commit)) {
                point.label = record->label;
            }
        }
        return point;
    };
    analysis.left_divergence = first_after_ancestor(*left_branch.head);
    analysis.right_divergence = first_after_ancestor(*right_branch.head);

    const auto ancestor_snapshot = store.snapshots().get(store.snapshots().materialize(*analysis.common_ancestor));
    const auto left_snapshot = store.world(left_id).snapshot();
    const auto right_snapshot = store.world(right_id).snapshot();

    const auto ancestor_objects = object_map(ancestor_snapshot);
    const auto left_objects = object_map(left_snapshot);
    const auto right_objects = object_map(right_snapshot);
    std::set<std::pair<ObjectIndex, Generation>> object_keys;
    for (const auto& [key, _] : ancestor_objects) {
        object_keys.insert(key);
    }
    for (const auto& [key, _] : left_objects) {
        object_keys.insert(key);
    }
    for (const auto& [key, _] : right_objects) {
        object_keys.insert(key);
    }

    for (const auto& key : object_keys) {
        const auto ancestor = ancestor_objects.contains(key) ? ancestor_objects.at(key) : nullptr;
        const auto left = left_objects.contains(key) ? left_objects.at(key) : nullptr;
        const auto right = right_objects.contains(key) ? right_objects.at(key) : nullptr;
        const auto left_type = type_of(left);
        const auto right_type = type_of(right);
        const auto left_type_name = type_name_of(left_snapshot, left);
        const auto right_type_name = type_name_of(right_snapshot, right);
        const auto left_existence = existence_of(left);
        const auto right_existence = existence_of(right);
        if (left_type_name != right_type_name || left_existence != right_existence) {
            analysis.object_conflicts.push_back(ObjectConflict{
                ObjectId{key.first, key.second},
                name_of(ancestor, left, right),
                left_type_name != right_type_name ? "object type diverged" : "object existence diverged",
                type_of(ancestor),
                left_type,
                right_type,
                existence_of(ancestor),
                left_existence,
                right_existence
            });
        }
    }

    const auto ancestor_pointers = pointer_map(ancestor_snapshot);
    const auto left_pointers = pointer_map(left_snapshot);
    const auto right_pointers = pointer_map(right_snapshot);
    std::set<std::uint64_t> pointer_keys;
    for (const auto& [key, _] : ancestor_pointers) {
        pointer_keys.insert(key);
    }
    for (const auto& [key, _] : left_pointers) {
        pointer_keys.insert(key);
    }
    for (const auto& [key, _] : right_pointers) {
        pointer_keys.insert(key);
    }

    for (const auto key : pointer_keys) {
        const auto ancestor = ancestor_pointers.contains(key) ? ancestor_pointers.at(key) : nullptr;
        const auto left = left_pointers.contains(key) ? left_pointers.at(key) : nullptr;
        const auto right = right_pointers.contains(key) ? right_pointers.at(key) : nullptr;
        if (!same_pointer(left, right) && changed_from_ancestor(ancestor, left) && changed_from_ancestor(ancestor, right)) {
            analysis.pointer_conflicts.push_back(PointerConflict{PointerId{key}, "pointer diverged"});
        }
    }

    const auto* left_record = store.commit_record(*left_branch.head);
    const auto* right_record = store.commit_record(*right_branch.head);
    if (left_record != nullptr && right_record != nullptr && left_record->law_hash != right_record->law_hash) {
        analysis.law_drifts.push_back(LawDrift{left_record->law_hash, right_record->law_hash});
    }

    analysis.status = analysis.object_conflicts.empty()
            && analysis.pointer_conflicts.empty()
            && analysis.predicted_violations.empty()
            && analysis.law_drifts.empty()
        ? MergeStatus::Clean
        : MergeStatus::Conflict;
    return analysis;
}

std::string to_string(MergeStatus status) {
    switch (status) {
    case MergeStatus::Clean:
        return "Clean";
    case MergeStatus::Conflict:
        return "Conflict";
    case MergeStatus::LawRejected:
        return "LawRejected";
    case MergeStatus::DivergentHistory:
        return "DivergentHistory";
    }
    return "Conflict";
}

}  // namespace pv
