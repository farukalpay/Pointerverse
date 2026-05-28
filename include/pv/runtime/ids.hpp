// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>

#include "pv/hash/canonical.hpp"

namespace pv {

struct BranchId {
    std::uint64_t value{0};

    [[nodiscard]] bool valid() const noexcept { return value != 0; }

    friend bool operator==(BranchId, BranchId) = default;
};

struct TransactionId {
    std::uint64_t value{0};

    [[nodiscard]] bool valid() const noexcept { return value != 0; }

    friend bool operator==(TransactionId, TransactionId) = default;
};

struct CommitId {
    Hash256 value;

    [[nodiscard]] bool valid() const noexcept { return !empty(value); }

    friend bool operator==(const CommitId&, const CommitId&) = default;
};

struct SnapshotId {
    std::uint64_t value{0};

    [[nodiscard]] bool valid() const noexcept { return value != 0; }

    friend bool operator==(SnapshotId, SnapshotId) = default;
};

[[nodiscard]] std::string to_string(BranchId id);
[[nodiscard]] std::string to_string(TransactionId id);
[[nodiscard]] std::string to_string(CommitId id);
[[nodiscard]] std::string to_string(SnapshotId id);

}  // namespace pv
