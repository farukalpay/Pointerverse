// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pv/core/id.hpp"
#include "pv/runtime/ids.hpp"
#include "pv/runtime/transaction_types.hpp"
#include "pv/storage/index_store.hpp"

namespace pv {

struct CommitIndexEntry {
    CommitId id;
    Hash256 object;
    std::string branch;
    std::vector<CommitId> parents;
    Hash256 before_snapshot_object;
    Hash256 after_snapshot_object;
    Hash256 delta_object;
    Hash256 program_object;
    Epoch before_epoch;
    Epoch after_epoch;
    bool accepted{false};
    TransactionOrigin origin{TransactionOrigin::Manual};
};

struct BranchIndexEntry {
    std::string name;
    BranchId branch;
    CommitId head;
    Hash256 snapshot;
    Epoch epoch;
    std::vector<CommitId> history;
};

struct CommitIndexStats {
    std::size_t commits{0};
    std::size_t accepted_commits{0};
    std::size_t branches{0};
    std::size_t snapshots{0};
    std::size_t program_objects{0};
    std::size_t delta_objects{0};
};

class CommitIndex {
public:
    explicit CommitIndex(std::filesystem::path root);

    [[nodiscard]] bool exists() const;
    [[nodiscard]] std::vector<CommitIndexEntry> entries() const;
    [[nodiscard]] std::optional<CommitIndexEntry> find(CommitId id) const;
    [[nodiscard]] CommitIndexStats stats() const;
    [[nodiscard]] IndexFileStatus check() const;
    [[nodiscard]] Hash256 checksum() const;

    void write(std::vector<CommitIndexEntry> entries) const;
    void upsert(const CommitIndexEntry& entry) const;
    void remove() const;

private:
    IndexStore store_;
};

class BranchIndex {
public:
    explicit BranchIndex(std::filesystem::path root);

    [[nodiscard]] bool exists() const;
    [[nodiscard]] std::vector<BranchIndexEntry> entries() const;
    [[nodiscard]] std::optional<BranchIndexEntry> find(std::string_view branch) const;
    [[nodiscard]] IndexFileStatus check() const;
    [[nodiscard]] Hash256 checksum() const;

    void write(std::vector<BranchIndexEntry> entries) const;
    void upsert(const BranchIndexEntry& entry) const;
    void remove() const;

private:
    IndexStore store_;
};

}  // namespace pv
