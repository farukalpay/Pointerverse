// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "pv/hash/canonical.hpp"
#include "pv/law/verifier.hpp"
#include "pv/runtime/ids.hpp"
#include "pv/runtime/world_store.hpp"
#include "pv/trace/replayer.hpp"

namespace pv {

struct RuntimeReplayResult {
    BranchId branch;
    std::string branch_name;
    std::size_t events_read{0};
    std::size_t events_replayed{0};
    std::size_t metadata_events{0};
    std::size_t commits_replayed{0};
    std::vector<ReplayError> errors;
    Hash256 final_hash;
};

class RuntimeReplayer {
public:
    [[nodiscard]] RuntimeReplayResult replay_into(
        WorldStore& store,
        BranchId branch,
        std::string_view jsonl,
        const Verifier& verifier) const;
};

}  // namespace pv
