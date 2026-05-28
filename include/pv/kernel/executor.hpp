// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pv/hash/canonical.hpp"
#include "pv/kernel/execution_plan.hpp"
#include "pv/kernel/proof.hpp"

namespace pv {

struct SealedExecutionPlan {
    ExecutionPlan plan;
    CommitProof proof;
    Hash256 proof_hash;
};

[[nodiscard]] SealedExecutionPlan seal_execution_plan(ExecutionPlan plan);

}  // namespace pv
