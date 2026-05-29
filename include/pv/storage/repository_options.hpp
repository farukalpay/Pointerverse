// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pv/runtime/checkpoint_policy.hpp"

namespace pv {

struct RepositoryOptions {
    CheckpointPolicy checkpoint_policy;
};

}  // namespace pv
