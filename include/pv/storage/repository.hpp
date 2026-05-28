// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pv/core/world.hpp"
#include "pv/runtime/fork.hpp"
#include "pv/runtime/merge.hpp"
#include "pv/runtime/replayer.hpp"
#include "pv/runtime/world_store.hpp"
#include "pv/storage/content_store.hpp"
#include "pv/storage/manifest.hpp"
#include "pv/storage/ref_store.hpp"
#include "pv/storage/wal.hpp"

namespace pv {

struct RepositoryStatus {
    std::filesystem::path root;
    std::string current_branch;
    std::size_t branches{0};
};

class Repository {
public:
    static Repository init(std::filesystem::path root);
    static Repository open(std::filesystem::path root);

    [[nodiscard]] const std::filesystem::path& root() const noexcept;
    [[nodiscard]] RepositoryStatus status() const;

    [[nodiscard]] BranchId create_branch(std::string name, World initial);
    [[nodiscard]] ForkResult fork(std::string_view source, std::string new_name);
    [[nodiscard]] std::optional<CommitRecord> commit(
        std::string_view branch,
        Transaction tx,
        const Verifier& verifier);
    [[nodiscard]] RuntimeReplayResult replay_trace(
        std::string_view branch,
        std::string_view jsonl,
        const Verifier& verifier);

    [[nodiscard]] MergeAnalysis analyze_merge(std::string_view left, std::string_view right) const;
    [[nodiscard]] std::vector<BranchRef> list_branches() const;
    [[nodiscard]] bool has_branch(std::string_view name) const;
    [[nodiscard]] std::vector<CommitRecord> history(std::string_view branch) const;
    [[nodiscard]] const World& world(std::string_view branch) const;
    [[nodiscard]] World& mutable_world(std::string_view branch);

    void checkout(std::string_view branch);
    [[nodiscard]] std::string current_branch() const;

    [[nodiscard]] ContentStore& objects() noexcept;
    [[nodiscard]] const ContentStore& objects() const noexcept;
    [[nodiscard]] const RefStore& refs() const noexcept;
    [[nodiscard]] const WorldStore& runtime() const noexcept;

private:
    explicit Repository(std::filesystem::path root);

    void load();
    void persist_record(
        std::string_view branch,
        const CommitRecord& record,
        const Delta& delta,
        const std::vector<std::string>& morphism_path);
    void write_history(std::string_view branch, const std::vector<CommitRecord>& history) const;
    [[nodiscard]] std::vector<CommitId> read_history_ids(std::string_view branch) const;
    [[nodiscard]] std::filesystem::path history_path(std::string_view branch) const;
    [[nodiscard]] BranchId require_branch(std::string_view name) const;
    [[nodiscard]] BranchRef branch_ref_from_runtime(std::string_view branch) const;

    std::filesystem::path root_;
    ManifestStore manifest_;
    ContentStore objects_;
    RefStore refs_;
    Wal wal_;
    WorldStore store_;
};

}  // namespace pv
