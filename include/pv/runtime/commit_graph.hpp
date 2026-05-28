// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <vector>

#include "pv/runtime/ids.hpp"

namespace pv {

struct CommitNode {
    CommitId id;
    std::vector<CommitId> parents;
    std::vector<CommitId> children;
    BranchId branch;
    SnapshotId snapshot;
};

class CommitGraph {
public:
    void add_node(CommitNode node);

    [[nodiscard]] bool contains(CommitId id) const noexcept;
    [[nodiscard]] const CommitNode* node(CommitId id) const noexcept;
    [[nodiscard]] std::vector<CommitId> path_to_root(CommitId id) const;
    [[nodiscard]] std::optional<CommitId> common_ancestor(CommitId left, CommitId right) const;

private:
    std::vector<CommitNode> nodes_;
};

}  // namespace pv
