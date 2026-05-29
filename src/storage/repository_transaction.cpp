// SPDX-License-Identifier: Apache-2.0
#include "pv/storage/repository_transaction.hpp"

#include <fstream>
#include <stdexcept>
#include <utility>

#include "pv/hash/canonical.hpp"
#include "pv/storage/chunked_snapshot_store.hpp"
#include "pv/storage/object_codec.hpp"

namespace pv {
namespace {

std::vector<std::byte> text_payload(std::string_view value) {
    std::vector<std::byte> out;
    out.reserve(value.size());
    for (const auto ch : value) {
        out.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }
    return out;
}

std::filesystem::path history_path(const std::filesystem::path& root, std::string_view branch) {
    if (!RefStore::valid_branch_name(branch)) {
        throw std::invalid_argument("invalid branch name");
    }
    return root / "history" / "branches" / std::filesystem::path{std::string{branch}};
}

void write_history_mirror(const std::filesystem::path& root, std::string_view branch, const std::vector<CommitId>& history) {
    const auto path = history_path(root, branch);
    std::filesystem::create_directories(path.parent_path());
    const auto tmp = path.string() + ".tmp";
    std::ofstream output(tmp, std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot write branch history mirror");
    }
    for (const auto id : history) {
        output << to_hex(id.value) << '\n';
    }
    output.close();
    std::filesystem::rename(tmp, path);
}

CommitIndexEntry make_commit_entry(std::string_view branch, const CommitRecord& record, const StoredCommit& stored) {
    CommitIndexEntry entry;
    entry.id = record.id;
    entry.object = record.id.value;
    entry.branch = std::string{branch};
    entry.parents = record.parents;
    entry.before_snapshot_object = stored.before_snapshot_object;
    entry.after_snapshot_object = stored.after_snapshot_object;
    entry.delta_object = stored.delta_object;
    entry.program_object = stored.program_object;
    entry.before_root = record.before_root;
    entry.after_root = record.after_root;
    entry.checkpoint_snapshot_object = record.checkpoint_snapshot_object;
    entry.checkpoint_distance = record.checkpoint_distance;
    entry.graph_page_roots = record.graph_page_roots;
    entry.before_epoch = record.before_epoch;
    entry.after_epoch = record.after_epoch;
    entry.accepted = record.accepted;
    entry.origin = record.origin;
    return entry;
}

BranchIndexEntry make_branch_entry(const BranchRef& ref, std::vector<CommitId> history) {
    return BranchIndexEntry{
        ref.name,
        ref.branch,
        ref.head,
        ref.snapshot,
        ref.epoch,
        std::move(history)
    };
}

}  // namespace

RepositoryTransactionWriter::RepositoryTransactionWriter(
    std::filesystem::path root,
    ContentStore& objects,
    RefStore& refs,
    Wal& wal,
    CommitIndex& commits,
    BranchIndex& branches,
    WorldIndexStore& world_index,
    EventIndexStore& events)
    : root_(std::move(root)),
      objects_(objects),
      refs_(refs),
      wal_(wal),
      commits_(commits),
      branches_(branches),
      world_index_(world_index),
      events_(events) {}

RepositoryCommitObjects RepositoryTransactionWriter::write_commit(const RepositoryCommitWrite& write) const {
    auto stored = make_stored_commit(write.record);

    wal_.append(WalOp::BeginCommit, text_payload(to_hex(write.record.id.value)));
    stored.delta_object = objects_.put_canonical(write.delta);
    wal_.append(WalOp::PutObject, stored.delta_object.value);
    if (write.program.has_value()) {
        stored.program_object = objects_.put_canonical(*write.program);
        if (stored.program_object != write.record.program_hash) {
            throw std::runtime_error("stored program hash does not match commit record");
        }
        wal_.append(WalOp::PutObject, stored.program_object.value);
    }
    stored.trace_object = objects_.put_canonical(write.record.events);
    wal_.append(WalOp::PutObject, stored.trace_object.value);
    stored.law_status_object = objects_.put_canonical(write.record.law_statuses);
    wal_.append(WalOp::PutObject, stored.law_status_object.value);
    stored.violation_object = objects_.put_canonical(write.record.violations);
    wal_.append(WalOp::PutObject, stored.violation_object.value);
    stored.morphism_path_object = objects_.put_bytes(canonical_encode_morphism_path(write.morphism_path));
    wal_.append(WalOp::PutObject, stored.morphism_path_object.value);

    if (!empty(write.record.checkpoint_snapshot_object)) {
        const auto checkpoint = ChunkedSnapshotStore{objects_}.put_snapshot(write.after_snapshot);
        if (checkpoint != write.record.checkpoint_snapshot_object) {
            throw std::runtime_error("stored checkpoint hash does not match commit record");
        }
        wal_.append(WalOp::PutObject, checkpoint.value);
    }

    const auto commit_object = objects_.put_canonical(stored);
    if (commit_object != write.record.id.value) {
        throw std::runtime_error("stored commit hash does not match commit id");
    }
    wal_.append(WalOp::PutObject, commit_object.value);
    wal_.append(WalOp::BindSnapshot, write.record.after_hash.value);

    commits_.upsert(make_commit_entry(write.branch, write.record, stored));
    events_.index_commit(write.branch, write.record, write.after_snapshot);
    world_index_.index_commit(write.record.id, write.record.after_root, write.after_snapshot);

    if (write.accepted_ref.has_value()) {
        refs_.update_branch(*write.accepted_ref);
        branches_.upsert(make_branch_entry(*write.accepted_ref, write.branch_history));
        world_index_.update_branch(write.accepted_ref->name, write.accepted_ref->snapshot, write.after_snapshot);
        wal_.append(WalOp::UpdateBranchRef, text_payload(write.accepted_ref->name));
    } else if (auto branch = branches_.find(write.branch); branch.has_value()) {
        branch->history = write.branch_history;
        branches_.upsert(*branch);
    }

    write_history_mirror(root_, write.branch, write.branch_history);
    wal_.append(WalOp::AddCommitNode, write.record.id.value.value);
    wal_.append(WalOp::EndCommit, write.record.id.value.value);
    wal_.truncate();
    return RepositoryCommitObjects{std::move(stored), commit_object};
}

}  // namespace pv
