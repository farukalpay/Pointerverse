// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <span>
#include <string_view>
#include <vector>

#include "pv/core/snapshot.hpp"
#include "pv/runtime/ids.hpp"
#include "pv/storage/commit_index.hpp"
#include "pv/storage/content_store.hpp"
#include "pv/storage/object_codec.hpp"

namespace pv {

class SnapshotMaterializer {
public:
    SnapshotMaterializer(ContentStore& objects, const CommitIndex& commits, const BranchIndex& branches);

    [[nodiscard]] WorldSnapshot materialize_branch(std::string_view branch) const;
    [[nodiscard]] WorldSnapshot materialize_commit(CommitId commit) const;
    [[nodiscard]] std::vector<std::pair<CommitId, WorldSnapshot>> materialize_accepted_chain_to(CommitId commit) const;

private:
    [[nodiscard]] WorldSnapshot load_checkpoint(CommitId nearest) const;
    [[nodiscard]] WorldSnapshot apply_delta_chain(WorldSnapshot base, std::span<const CommitId> chain) const;
    [[nodiscard]] std::vector<CommitId> parent_chain(CommitId head) const;
    [[nodiscard]] bool is_checkpoint(const StoredCommit& stored) const;

    ContentStore& objects_;
    const CommitIndex& commits_;
    const BranchIndex& branches_;
};

}  // namespace pv
