// SPDX-License-Identifier: Apache-2.0
#include "pv/storage/commit_index.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "pv/hash/canonical.hpp"

namespace pv {
namespace {

std::string key(CommitId id) {
    return to_hex(id.value);
}

void write_commit_id(IndexPayloadWriter& writer, CommitId id) {
    writer.hash(id.value);
}

CommitId read_commit_id(IndexPayloadReader& reader) {
    return CommitId{reader.hash()};
}

void write_commit_ids(IndexPayloadWriter& writer, const std::vector<CommitId>& ids) {
    writer.u64(ids.size());
    for (const auto id : ids) {
        write_commit_id(writer, id);
    }
}

std::vector<CommitId> read_commit_ids(IndexPayloadReader& reader) {
    const auto size = reader.u64();
    std::vector<CommitId> ids;
    ids.reserve(static_cast<std::size_t>(size));
    for (std::uint64_t index = 0; index < size; ++index) {
        ids.push_back(read_commit_id(reader));
    }
    return ids;
}

void write_hashes(IndexPayloadWriter& writer, const std::vector<Hash256>& hashes) {
    writer.u64(hashes.size());
    for (const auto hash : hashes) {
        writer.hash(hash);
    }
}

std::vector<Hash256> read_hashes(IndexPayloadReader& reader) {
    const auto size = reader.u64();
    std::vector<Hash256> hashes;
    hashes.reserve(static_cast<std::size_t>(size));
    for (std::uint64_t index = 0; index < size; ++index) {
        hashes.push_back(reader.hash());
    }
    return hashes;
}

std::uint8_t origin_to_u8(TransactionOrigin origin) {
    switch (origin) {
    case TransactionOrigin::Manual:
        return 0;
    case TransactionOrigin::Script:
        return 1;
    case TransactionOrigin::Replay:
        return 2;
    case TransactionOrigin::Evolution:
        return 3;
    case TransactionOrigin::Morphism:
        return 4;
    case TransactionOrigin::Ingestion:
        return 5;
    case TransactionOrigin::Internal:
        return 6;
    case TransactionOrigin::ForkMerge:
        return 7;
    }
    return 0;
}

TransactionOrigin origin_from_u8(std::uint8_t value) {
    switch (value) {
    case 0:
        return TransactionOrigin::Manual;
    case 1:
        return TransactionOrigin::Script;
    case 2:
        return TransactionOrigin::Replay;
    case 3:
        return TransactionOrigin::Evolution;
    case 4:
        return TransactionOrigin::Morphism;
    case 5:
        return TransactionOrigin::Ingestion;
    case 6:
        return TransactionOrigin::Internal;
    case 7:
        return TransactionOrigin::ForkMerge;
    default:
        throw std::runtime_error("invalid transaction origin in commit index");
    }
}

void sort_commit_entries(std::vector<CommitIndexEntry>& entries) {
    std::ranges::sort(entries, [](const CommitIndexEntry& left, const CommitIndexEntry& right) {
        return key(left.id) < key(right.id);
    });
}

void sort_branch_entries(std::vector<BranchIndexEntry>& entries) {
    std::ranges::sort(entries, [](const BranchIndexEntry& left, const BranchIndexEntry& right) {
        return left.name < right.name;
    });
}

}  // namespace

CommitIndex::CommitIndex(std::filesystem::path root)
    : store_(std::move(root), "commits.idx", "PVCOMMITIDX2") {}

bool CommitIndex::exists() const {
    return store_.exists();
}

std::vector<CommitIndexEntry> CommitIndex::entries() const {
    if (!exists()) {
        return {};
    }
    const auto payload = store_.read_payload();
    IndexPayloadReader reader{payload};
    const auto size = reader.u64();
    std::vector<CommitIndexEntry> entries;
    entries.reserve(static_cast<std::size_t>(size));
    for (std::uint64_t index = 0; index < size; ++index) {
        CommitIndexEntry entry;
        entry.id = read_commit_id(reader);
        entry.object = reader.hash();
        entry.branch = reader.string();
        entry.parents = read_commit_ids(reader);
        entry.before_snapshot_object = reader.hash();
        entry.after_snapshot_object = reader.hash();
        entry.delta_object = reader.hash();
        entry.program_object = reader.hash();
        entry.before_root = reader.hash();
        entry.after_root = reader.hash();
        entry.checkpoint_snapshot_object = reader.hash();
        entry.checkpoint_distance = reader.u64();
        entry.graph_page_roots = read_hashes(reader);
        entry.before_epoch = Epoch{reader.u64()};
        entry.after_epoch = Epoch{reader.u64()};
        entry.accepted = reader.boolean();
        entry.origin = origin_from_u8(reader.u8());
        entries.push_back(std::move(entry));
    }
    reader.expect_end();
    return entries;
}

std::optional<CommitIndexEntry> CommitIndex::find(CommitId id) const {
    for (const auto& entry : entries()) {
        if (entry.id == id) {
            return entry;
        }
    }
    return std::nullopt;
}

CommitIndexStats CommitIndex::stats() const {
    CommitIndexStats out;
    const auto all = entries();
    out.commits = all.size();
    for (const auto& entry : all) {
        if (entry.accepted) {
            out.accepted_commits += 1;
            if (entry.checkpoint_distance == 0 && !empty(entry.checkpoint_snapshot_object)) {
                out.snapshots += 1;
            }
        }
        if (!empty(entry.program_object)) {
            out.program_objects += 1;
        }
        if (!empty(entry.delta_object)) {
            out.delta_objects += 1;
        }
    }
    return out;
}

IndexFileStatus CommitIndex::check() const {
    return store_.check();
}

Hash256 CommitIndex::checksum() const {
    return store_.checksum();
}

void CommitIndex::write(std::vector<CommitIndexEntry> entries) const {
    sort_commit_entries(entries);
    entries.erase(std::ranges::unique(entries, {}, &CommitIndexEntry::id).begin(), entries.end());
    IndexPayloadWriter writer;
    writer.u64(entries.size());
    for (const auto& entry : entries) {
        write_commit_id(writer, entry.id);
        writer.hash(entry.object);
        writer.string(entry.branch);
        write_commit_ids(writer, entry.parents);
        writer.hash(entry.before_snapshot_object);
        writer.hash(entry.after_snapshot_object);
        writer.hash(entry.delta_object);
        writer.hash(entry.program_object);
        writer.hash(entry.before_root);
        writer.hash(entry.after_root);
        writer.hash(entry.checkpoint_snapshot_object);
        writer.u64(entry.checkpoint_distance);
        write_hashes(writer, entry.graph_page_roots);
        writer.u64(entry.before_epoch.value);
        writer.u64(entry.after_epoch.value);
        writer.boolean(entry.accepted);
        writer.u8(origin_to_u8(entry.origin));
    }
    store_.write_payload(writer.bytes());
}

void CommitIndex::upsert(const CommitIndexEntry& entry) const {
    auto all = entries();
    auto iter = std::ranges::find(all, entry.id, &CommitIndexEntry::id);
    if (iter == all.end()) {
        all.push_back(entry);
    } else {
        *iter = entry;
    }
    write(std::move(all));
}

void CommitIndex::remove() const {
    store_.remove();
}

BranchIndex::BranchIndex(std::filesystem::path root)
    : store_(std::move(root), "branches.idx", "PVBRANCHIDX1") {}

bool BranchIndex::exists() const {
    return store_.exists();
}

std::vector<BranchIndexEntry> BranchIndex::entries() const {
    if (!exists()) {
        return {};
    }
    const auto payload = store_.read_payload();
    IndexPayloadReader reader{payload};
    const auto size = reader.u64();
    std::vector<BranchIndexEntry> entries;
    entries.reserve(static_cast<std::size_t>(size));
    for (std::uint64_t index = 0; index < size; ++index) {
        BranchIndexEntry entry;
        entry.name = reader.string();
        entry.branch = BranchId{reader.u64()};
        entry.head = read_commit_id(reader);
        entry.snapshot = reader.hash();
        entry.epoch = Epoch{reader.u64()};
        entry.history = read_commit_ids(reader);
        entries.push_back(std::move(entry));
    }
    reader.expect_end();
    return entries;
}

std::optional<BranchIndexEntry> BranchIndex::find(std::string_view branch) const {
    for (const auto& entry : entries()) {
        if (entry.name == branch) {
            return entry;
        }
    }
    return std::nullopt;
}

IndexFileStatus BranchIndex::check() const {
    return store_.check();
}

Hash256 BranchIndex::checksum() const {
    return store_.checksum();
}

void BranchIndex::write(std::vector<BranchIndexEntry> entries) const {
    sort_branch_entries(entries);
    entries.erase(std::ranges::unique(entries, {}, &BranchIndexEntry::name).begin(), entries.end());
    IndexPayloadWriter writer;
    writer.u64(entries.size());
    for (const auto& entry : entries) {
        writer.string(entry.name);
        writer.u64(entry.branch.value);
        write_commit_id(writer, entry.head);
        writer.hash(entry.snapshot);
        writer.u64(entry.epoch.value);
        write_commit_ids(writer, entry.history);
    }
    store_.write_payload(writer.bytes());
}

void BranchIndex::upsert(const BranchIndexEntry& entry) const {
    auto all = entries();
    auto iter = std::ranges::find(all, entry.name, &BranchIndexEntry::name);
    if (iter == all.end()) {
        all.push_back(entry);
    } else {
        *iter = entry;
    }
    write(std::move(all));
}

void BranchIndex::remove() const {
    store_.remove();
}

}  // namespace pv
