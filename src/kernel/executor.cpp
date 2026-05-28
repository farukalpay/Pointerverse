// SPDX-License-Identifier: Apache-2.0
#include "pv/kernel/executor.hpp"

#include <utility>

namespace pv {

SealedExecutionPlan seal_execution_plan(ExecutionPlan plan) {
    plan.plan_hash = hash_execution_plan(plan);
    auto proof = make_commit_proof(plan);
    return SealedExecutionPlan{std::move(plan), proof, hash_commit_proof(proof)};
}

}  // namespace pv
