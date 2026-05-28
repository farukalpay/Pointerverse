// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "pv/core/delta.hpp"
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
    Internal,
    Ingestion
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

}  // namespace pv
