// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pv/core/world.hpp"
#include "pv/runtime/commit_record.hpp"
#include "pv/storage/commit_index.hpp"
#include "pv/storage/content_store.hpp"
#include "pv/storage/event_index.hpp"
#include "pv/storage/ref_store.hpp"
#include "pv/storage/wal.hpp"
#include "pv/storage/world_index_store.hpp"

namespace pv {

struct MaterializedWorld {
    BranchRef ref;
    std::vector<CommitRecord> history;
    std::vector<std::pair<CommitId, WorldSnapshot>> snapshots;
    World world;
};

struct RepositoryIndexCheck {
    bool clean{true};
    std::vector<std::string> messages;
    Hash256 commits_checksum;
    Hash256 branches_checksum;
    Hash256 events_checksum;
    Hash256 objects_checksum;
    Hash256 relations_checksum;
};

struct RepositoryBackendStats {
    std::size_t objects{0};
    std::size_t commits{0};
    std::size_t branches{0};
    std::size_t snapshots{0};
    std::size_t program_objects{0};
    std::size_t delta_objects{0};
    std::string index_status{"missing"};
    std::string wal_status{"clean"};
};

class RepositoryEngine {
public:
    RepositoryEngine(
        std::filesystem::path root,
        ContentStore& objects,
        RefStore& refs,
        Wal& wal);

    void open();

    [[nodiscard]] bool has_branch(std::string_view branch) const;
    [[nodiscard]] std::optional<BranchRef> branch_ref(std::string_view branch) const;
    [[nodiscard]] std::vector<BranchRef> list_branches() const;
    [[nodiscard]] std::vector<CommitRecord> history(std::string_view branch) const;
    [[nodiscard]] CommitRecord commit_record(CommitId id) const;
    [[nodiscard]] StoredCommit stored_commit(CommitId id) const;
    [[nodiscard]] WorldSnapshot snapshot(std::string_view branch) const;
    [[nodiscard]] WorldSnapshot snapshot(CommitId commit) const;
    [[nodiscard]] MaterializedWorld materialize_branch(std::string_view branch) const;
    [[nodiscard]] MaterializedWorld materialize_commit(CommitId commit) const;

    [[nodiscard]] RepositoryIndexCheck check_indexes() const;
    [[nodiscard]] RepositoryBackendStats stats() const;

    void rebuild_indexes() const;
    void compact() const;

    [[nodiscard]] CommitIndex& commits() noexcept;
    [[nodiscard]] BranchIndex& branches() noexcept;
    [[nodiscard]] WorldIndexStore& world_index() noexcept;
    [[nodiscard]] EventIndexStore& events() noexcept;
    [[nodiscard]] const CommitIndex& commits() const noexcept;
    [[nodiscard]] const BranchIndex& branches() const noexcept;
    [[nodiscard]] const WorldIndexStore& world_index() const noexcept;
    [[nodiscard]] const EventIndexStore& events() const noexcept;

private:
    [[nodiscard]] std::vector<CommitId> read_history_ids(std::string_view branch) const;
    [[nodiscard]] std::vector<CommitId> parent_chain(CommitId head) const;
    [[nodiscard]] std::optional<CommitId> latest_accepted(const std::vector<CommitId>& ids) const;

    std::filesystem::path root_;
    ContentStore& objects_;
    RefStore& refs_;
    Wal& wal_;
    mutable CommitIndex commits_;
    mutable BranchIndex branches_;
    mutable WorldIndexStore world_index_;
    mutable EventIndexStore events_;
};

}  // namespace pv
