// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "pv/core/object.hpp"
#include "pv/core/pointer.hpp"
#include "pv/hash/canonical.hpp"
#include "pv/law/law.hpp"
#include "pv/runtime/ids.hpp"

namespace pv {

class WorldStore;

enum class MergeStatus {
    Clean,
    Conflict,
    LawRejected,
    DivergentHistory
};

struct ObjectConflict {
    ObjectId object;
    std::string name;
    std::string reason;
    std::optional<TypeId> ancestor_type;
    std::optional<TypeId> left_type;
    std::optional<TypeId> right_type;
    std::optional<ExistenceState> ancestor_existence;
    std::optional<ExistenceState> left_existence;
    std::optional<ExistenceState> right_existence;
};

struct PointerConflict {
    PointerId pointer;
    std::string reason;
};

struct LawDrift {
    Hash256 left_law_hash;
    Hash256 right_law_hash;
};

// The first commit on a branch after the common ancestor: where this branch
// began to diverge, and therefore the first causally relevant change on its side.
struct DivergencePoint {
    std::optional<CommitId> commit;
    std::string label;
};

struct MergeAnalysis {
    BranchId left;
    BranchId right;
    std::optional<CommitId> common_ancestor;
    DivergencePoint left_divergence;
    DivergencePoint right_divergence;
    std::vector<ObjectConflict> object_conflicts;
    std::vector<PointerConflict> pointer_conflicts;
    std::vector<LawViolation> predicted_violations;
    std::vector<LawDrift> law_drifts;
    MergeStatus status{MergeStatus::Clean};
};

[[nodiscard]] MergeAnalysis analyze_merge(const WorldStore& store, BranchId left, BranchId right);
[[nodiscard]] std::string to_string(MergeStatus status);

}  // namespace pv
