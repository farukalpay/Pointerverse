// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <utility>
#include <vector>

#include "pv/core/snapshot.hpp"
#include "pv/runtime/ids.hpp"

namespace pv {

class SnapshotStore {
public:
    virtual ~SnapshotStore() = default;

    [[nodiscard]] virtual SnapshotId put(WorldSnapshot snapshot) = 0;
    [[nodiscard]] virtual const WorldSnapshot& get(SnapshotId id) const = 0;
    [[nodiscard]] virtual SnapshotId materialize(CommitId id) const = 0;
    virtual void bind_commit(CommitId commit, SnapshotId snapshot) = 0;
};

class InMemorySnapshotStore final : public SnapshotStore {
public:
    [[nodiscard]] SnapshotId put(WorldSnapshot snapshot) override;
    [[nodiscard]] const WorldSnapshot& get(SnapshotId id) const override;
    [[nodiscard]] SnapshotId materialize(CommitId id) const override;
    void bind_commit(CommitId commit, SnapshotId snapshot) override;

private:
    std::vector<WorldSnapshot> snapshots_;
    std::vector<std::pair<CommitId, SnapshotId>> commit_snapshots_;
};

}  // namespace pv
