// SPDX-License-Identifier: Apache-2.0
#include "pv/runtime/snapshot_store.hpp"

#include <fmt/format.h>

#include <stdexcept>

namespace pv {

SnapshotId InMemorySnapshotStore::put(WorldSnapshot snapshot) {
    snapshots_.push_back(std::move(snapshot));
    return SnapshotId{snapshots_.size()};
}

const WorldSnapshot& InMemorySnapshotStore::get(SnapshotId id) const {
    if (!id.valid() || id.value > snapshots_.size()) {
        throw std::out_of_range(fmt::format("unknown snapshot {}", to_string(id)));
    }
    return snapshots_[id.value - 1];
}

SnapshotId InMemorySnapshotStore::materialize(CommitId id) const {
    for (const auto& [commit, snapshot] : commit_snapshots_) {
        if (commit == id) {
            return snapshot;
        }
    }
    throw std::out_of_range(fmt::format("no snapshot bound to commit {}", to_string(id)));
}

void InMemorySnapshotStore::bind_commit(CommitId commit, SnapshotId snapshot) {
    for (auto& [existing_commit, existing_snapshot] : commit_snapshots_) {
        if (existing_commit == commit) {
            existing_snapshot = snapshot;
            return;
        }
    }
    commit_snapshots_.push_back({commit, snapshot});
}

}  // namespace pv
