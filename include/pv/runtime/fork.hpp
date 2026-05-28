// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "pv/hash/canonical.hpp"
#include "pv/runtime/ids.hpp"
#include "pv/trace/event.hpp"

namespace pv {

struct ForkResult {
    BranchId source;
    BranchId forked;
    CommitId base_commit;
    SnapshotId base_snapshot;
    Hash256 base_hash;
    std::vector<TraceEvent> events;
};

}  // namespace pv
