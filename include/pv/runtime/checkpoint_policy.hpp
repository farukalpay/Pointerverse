// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace pv {

struct CheckpointPolicy {
    std::uint64_t every_n_commits{64};
    std::uint64_t max_delta_chain{256};
    bool force_checkpoint_on_branch_fork{true};
};

}  // namespace pv
