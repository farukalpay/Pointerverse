// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "pv/core/fact.hpp"
#include "pv/core/operation.hpp"
#include "pv/core/snapshot.hpp"
#include "pv/hash/canonical.hpp"
#include "pv/kernel/proof.hpp"
#include "pv/law/verifier.hpp"
#include "pv/runtime/transaction_types.hpp"

namespace pv {

struct ResolvedOperation {
    OperationId id;
    OperationKind kind{OperationKind::EmitEvent};
    std::vector<ObjectId> touched_objects;
    std::vector<PointerId> touched_pointers;
    std::vector<FactId> reads;
    std::vector<FactId> writes;
};

struct ExecutionPlan {
    Transaction tx;
    WorldSnapshot before;
    std::vector<ResolvedOperation> resolved_ops;
    WorldSnapshot predicted_after;
    VerificationResult verification;
    Hash256 plan_hash;
};

[[nodiscard]] Hash256 hash_execution_plan(const ExecutionPlan& plan);
[[nodiscard]] CommitProof make_commit_proof(const ExecutionPlan& plan);
[[nodiscard]] std::vector<FactId> unique_fact_ids(std::vector<FactId> ids);

}  // namespace pv
