// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <expected>
#include <string>

#include "pv/core/world.hpp"
#include "pv/kernel/execution_plan.hpp"
#include "pv/runtime/ids.hpp"
#include "pv/runtime/transaction_types.hpp"

namespace pv {

struct PreparedTransaction {
    Transaction tx;
    WorldSnapshot before;
    std::expected<WorldSnapshot, OverlayError> predicted_after;
    ExecutionPlan execution_plan;
    VerificationResult verification;
    std::vector<TraceEvent> predicted_events;
    std::optional<CommitProof> proof;
    bool committable{false};
    std::string rejection_reason;
};

[[nodiscard]] PreparedTransaction prepare_transaction(const World& world, const Transaction& tx, const Verifier& verifier);
[[nodiscard]] CommitResult commit_prepared(World& world, const PreparedTransaction& prepared);
[[nodiscard]] std::string to_string(TransactionOrigin origin);

}  // namespace pv
