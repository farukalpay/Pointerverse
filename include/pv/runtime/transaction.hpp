// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <expected>
#include <optional>
#include <string>
#include <vector>

#include "pv/core/delta.hpp"
#include "pv/core/world.hpp"
#include "pv/hash/canonical.hpp"
#include "pv/runtime/ids.hpp"

namespace pv {

enum class TransactionOrigin : std::uint8_t {
    Manual,
    Script,
    Morphism,
    Evolution,
    Replay,
    ForkMerge,
    Internal
};

struct Transaction {
    TransactionId id;
    TransactionOrigin origin{TransactionOrigin::Manual};
    std::string label;
    Delta delta;
    std::vector<std::string> morphism_path;
    std::optional<Hash256> input_snapshot_hash;
    std::optional<Epoch> expected_base_epoch;
    bool allow_empty{false};
};

struct PreparedTransaction {
    Transaction tx;
    WorldSnapshot before;
    std::expected<WorldSnapshot, OverlayError> predicted_after;
    VerificationResult verification;
    std::vector<TraceEvent> predicted_events;
    bool committable{false};
    std::string rejection_reason;
};

[[nodiscard]] PreparedTransaction prepare_transaction(const World& world, const Transaction& tx, const Verifier& verifier);
[[nodiscard]] CommitResult commit_prepared(World& world, const PreparedTransaction& prepared);
[[nodiscard]] std::string to_string(TransactionOrigin origin);

}  // namespace pv
