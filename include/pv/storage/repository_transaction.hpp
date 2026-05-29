// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

#include "pv/core/delta.hpp"
#include "pv/core/world.hpp"
#include "pv/kernel/program.hpp"
#include "pv/runtime/commit_record.hpp"
#include "pv/storage/commit_index.hpp"
#include "pv/storage/content_store.hpp"
#include "pv/storage/event_index.hpp"
#include "pv/storage/ref_store.hpp"
#include "pv/storage/wal.hpp"
#include "pv/storage/world_index_store.hpp"

namespace pv {

struct RepositoryCommitWrite {
    std::string_view branch;
    CommitRecord record;
    Delta delta;
    std::optional<Program> program;
    std::vector<std::string> morphism_path;
    WorldSnapshot before_snapshot;
    WorldSnapshot after_snapshot;
    std::optional<BranchRef> accepted_ref;
    std::vector<CommitId> branch_history;
};

struct RepositoryCommitObjects {
    StoredCommit stored;
    Hash256 commit_object;
};

class RepositoryTransactionWriter {
public:
    RepositoryTransactionWriter(
        std::filesystem::path root,
        ContentStore& objects,
        RefStore& refs,
        Wal& wal,
        CommitIndex& commits,
        BranchIndex& branches,
        WorldIndexStore& world_index,
        EventIndexStore& events);

    [[nodiscard]] RepositoryCommitObjects write_commit(const RepositoryCommitWrite& write) const;

private:
    std::filesystem::path root_;
    ContentStore& objects_;
    RefStore& refs_;
    Wal& wal_;
    CommitIndex& commits_;
    BranchIndex& branches_;
    WorldIndexStore& world_index_;
    EventIndexStore& events_;
};

}  // namespace pv
