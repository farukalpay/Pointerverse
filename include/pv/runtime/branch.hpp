// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>

#include "pv/core/id.hpp"
#include "pv/runtime/ids.hpp"

namespace pv {

struct Branch {
    BranchId id;
    std::string name;
    WorldId world;
    Epoch epoch;
    std::optional<CommitId> head;
    SnapshotId head_snapshot;
};

}  // namespace pv
