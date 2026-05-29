// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "pv/core/id.hpp"
#include "pv/hash/canonical.hpp"
#include "pv/kernel/proof.hpp"
#include "pv/law/law.hpp"
#include "pv/runtime/ids.hpp"
#include "pv/runtime/transaction_types.hpp"
#include "pv/trace/event.hpp"

namespace pv {

struct CommitRecord {
    CommitId id;
    std::optional<CommitId> parent;
    std::vector<CommitId> parents;

    WorldId world;
    BranchId branch;
    std::string branch_name;
    TransactionId transaction;

    Epoch before_epoch;
    Epoch after_epoch;

    SnapshotId before_snapshot;
    SnapshotId after_snapshot;

    Hash256 before_hash;
    Hash256 after_hash;
    Hash256 delta_hash;
    Hash256 program_hash;
    Hash256 instruction_root;
    Hash256 symbol_table_hash;
    Hash256 trace_hash;
    Hash256 law_hash;
    Hash256 violation_hash;
    Hash256 morphism_path_hash;
    Hash256 execution_plan_hash;
    Hash256 read_set_hash;
    Hash256 write_set_hash;
    Hash256 proof_hash;
    std::optional<CommitProof> proof;
    Hash256 before_root;
    Hash256 after_root;
    Hash256 checkpoint_snapshot_object;
    std::uint64_t checkpoint_distance{0};
    std::vector<Hash256> graph_page_roots;

    bool accepted{false};

    TransactionOrigin origin{TransactionOrigin::Manual};
    std::string label;

    std::vector<LawStatus> law_statuses;
    std::vector<LawViolation> violations;
    std::vector<TraceEvent> events;
};

[[nodiscard]] CommitId make_commit_id(const CommitRecord& record);

}  // namespace pv
