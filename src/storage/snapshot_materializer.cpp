// SPDX-License-Identifier: Apache-2.0
#include "pv/storage/snapshot_materializer.hpp"

#include <algorithm>
#include <optional>
#include <stdexcept>

#include "pv/core/delta.hpp"
#include "pv/kernel/merkle.hpp"
#include "pv/storage/chunked_snapshot_store.hpp"

namespace pv {
namespace {

std::string commit_text(CommitId id) {
    return to_hex(id.value);
}

void verify_snapshot(const CommitRecord& record, const WorldSnapshot& snapshot) {
    if (snapshot.canonical_hash() != record.after_hash) {
        throw std::runtime_error("materialized snapshot hash mismatch for commit " + commit_text(record.id));
    }
    if (!empty(record.after_root) && compute_world_root(snapshot).root != record.after_root) {
        throw std::runtime_error("materialized snapshot root mismatch for commit " + commit_text(record.id));
    }
}

}  // namespace

SnapshotMaterializer::SnapshotMaterializer(ContentStore& objects, const CommitIndex& commits, const BranchIndex& branches)
    : objects_(objects), commits_(commits), branches_(branches) {}

WorldSnapshot SnapshotMaterializer::materialize_branch(std::string_view branch) const {
    const auto entry = branches_.find(branch);
    if (!entry.has_value()) {
        throw std::out_of_range("unknown branch '" + std::string{branch} + "'");
    }
    return materialize_commit(entry->head);
}

WorldSnapshot SnapshotMaterializer::materialize_commit(CommitId commit) const {
    const auto materialized = materialize_accepted_chain_to(commit);
    if (materialized.empty()) {
        throw std::runtime_error("commit has no materializable checkpoint: " + commit_text(commit));
    }
    return materialized.back().second;
}

std::vector<std::pair<CommitId, WorldSnapshot>> SnapshotMaterializer::materialize_accepted_chain_to(CommitId commit) const {
    const auto chain = parent_chain(commit);
    if (chain.empty()) {
        return {};
    }

    std::size_t checkpoint_index = chain.size();
    for (std::size_t index = chain.size(); index > 0; --index) {
        const auto candidate = chain[index - 1];
        const auto stored = objects_.get_canonical<StoredCommit>(candidate.value);
        if (is_checkpoint(stored)) {
            checkpoint_index = index - 1;
            break;
        }
    }
    if (checkpoint_index == chain.size()) {
        throw std::runtime_error("no checkpoint found for commit " + commit_text(commit));
    }

    std::vector<std::pair<CommitId, WorldSnapshot>> out;
    auto snapshot = load_checkpoint(chain[checkpoint_index]);
    auto checkpoint_stored = objects_.get_canonical<StoredCommit>(chain[checkpoint_index].value);
    checkpoint_stored.record.id = chain[checkpoint_index];
    verify_snapshot(checkpoint_stored.record, snapshot);
    if (checkpoint_stored.record.accepted) {
        out.push_back({chain[checkpoint_index], snapshot});
    }

    for (std::size_t index = checkpoint_index + 1; index < chain.size(); ++index) {
        const auto id = chain[index];
        auto stored = objects_.get_canonical<StoredCommit>(id.value);
        stored.record.id = id;
        if (!stored.record.accepted) {
            verify_snapshot(stored.record, snapshot);
            continue;
        }
        const auto delta = objects_.get_canonical<Delta>(stored.delta_object);
        auto next = apply_delta_to_snapshot(snapshot, delta);
        if (!next.has_value()) {
            throw std::runtime_error("delta application failed for commit " + commit_text(id));
        }
        verify_snapshot(stored.record, *next);
        snapshot = std::move(*next);
        out.push_back({id, snapshot});
    }
    return out;
}

WorldSnapshot SnapshotMaterializer::load_checkpoint(CommitId nearest) const {
    auto stored = objects_.get_canonical<StoredCommit>(nearest.value);
    stored.record.id = nearest;
    if (stored.format_version >= 4) {
        if (empty(stored.record.checkpoint_snapshot_object)) {
            throw std::runtime_error("commit is not a checkpoint: " + commit_text(nearest));
        }
        return ChunkedSnapshotStore{objects_}.get_snapshot(stored.record.checkpoint_snapshot_object);
    }
    return objects_.get_canonical<WorldSnapshot>(stored.after_snapshot_object);
}

WorldSnapshot SnapshotMaterializer::apply_delta_chain(WorldSnapshot base, std::span<const CommitId> chain) const {
    for (const auto commit : chain) {
        auto stored = objects_.get_canonical<StoredCommit>(commit.value);
        stored.record.id = commit;
        if (!stored.record.accepted) {
            continue;
        }
        const auto delta = objects_.get_canonical<Delta>(stored.delta_object);
        auto next = apply_delta_to_snapshot(base, delta);
        if (!next.has_value()) {
            throw std::runtime_error("delta application failed for commit " + commit_text(commit));
        }
        verify_snapshot(stored.record, *next);
        base = std::move(*next);
    }
    return base;
}

std::vector<CommitId> SnapshotMaterializer::parent_chain(CommitId head) const {
    std::vector<CommitId> reversed;
    auto current = std::optional<CommitId>{head};
    while (current.has_value()) {
        reversed.push_back(*current);
        auto stored = objects_.get_canonical<StoredCommit>(current->value);
        if (stored.record.parents.empty()) {
            current = std::nullopt;
        } else {
            current = stored.record.parents.front();
        }
    }
    std::ranges::reverse(reversed);
    return reversed;
}

bool SnapshotMaterializer::is_checkpoint(const StoredCommit& stored) const {
    if (stored.format_version >= 4) {
        return stored.record.accepted && !empty(stored.record.checkpoint_snapshot_object);
    }
    return stored.record.accepted && !empty(stored.after_snapshot_object);
}

}  // namespace pv
