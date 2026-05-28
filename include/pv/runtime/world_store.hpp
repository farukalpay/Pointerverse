// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "pv/core/world.hpp"
#include "pv/runtime/branch.hpp"
#include "pv/runtime/commit_graph.hpp"
#include "pv/runtime/commit_record.hpp"
#include "pv/runtime/fork.hpp"
#include "pv/runtime/merge.hpp"
#include "pv/runtime/snapshot_store.hpp"
#include "pv/runtime/transaction.hpp"

namespace pv {

class WorldStore {
public:
    [[nodiscard]] BranchId create_branch(std::string name, World initial);
    [[nodiscard]] BranchId restore_branch(
        BranchId id,
        std::string name,
        World head_world,
        std::vector<CommitRecord> history,
        std::vector<std::pair<CommitId, WorldSnapshot>> snapshots);
    [[nodiscard]] BranchId fork_branch(BranchId source, std::string new_name);
    [[nodiscard]] ForkResult fork(BranchId source, std::string new_name);

    [[nodiscard]] const World& world(BranchId branch) const;
    [[nodiscard]] World& mutable_world(BranchId branch);
    [[nodiscard]] const Branch& branch(BranchId branch) const;
    [[nodiscard]] std::optional<BranchId> find_branch(std::string_view name) const;

    [[nodiscard]] std::optional<CommitRecord> commit(BranchId branch, Transaction tx, const Verifier& verifier);
    [[nodiscard]] std::vector<CommitRecord> history(BranchId branch) const;
    [[nodiscard]] const CommitRecord* commit_record(CommitId id) const noexcept;
    [[nodiscard]] MergeAnalysis analyze_merge(BranchId left, BranchId right) const;

    [[nodiscard]] const CommitGraph& graph() const noexcept;
    [[nodiscard]] const SnapshotStore& snapshots() const noexcept;

private:
    struct BranchState {
        Branch branch;
        World world;
        std::vector<CommitRecord> history;
    };

    [[nodiscard]] BranchState& state(BranchId branch);
    [[nodiscard]] const BranchState& state(BranchId branch) const;

    std::vector<BranchState> branches_;
    InMemorySnapshotStore snapshots_;
    CommitGraph graph_;
    std::uint64_t next_branch_id_{1};
    std::uint64_t next_transaction_id_{1};
};

}  // namespace pv
