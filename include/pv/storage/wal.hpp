// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "pv/hash/canonical.hpp"

namespace pv {

enum class WalOp : std::uint8_t {
    BeginCommit,
    PutObject,
    BindSnapshot,
    AddCommitNode,
    UpdateBranchRef,
    EndCommit
};

struct WalEntry {
    std::uint64_t sequence{0};
    WalOp op{WalOp::BeginCommit};
    Hash256 payload_hash;
    std::vector<std::byte> payload;
};

struct WalRecoveryReport {
    std::size_t entries_read{0};
    bool incomplete_commit{false};
};

class Wal {
public:
    explicit Wal(std::filesystem::path root);

    void append(WalOp op, std::span<const std::byte> payload);
    [[nodiscard]] std::vector<WalEntry> read_all() const;
    [[nodiscard]] WalRecoveryReport recover() const;
    void truncate() const;

private:
    [[nodiscard]] std::filesystem::path path() const;

    std::filesystem::path root_;
};

[[nodiscard]] std::string to_string(WalOp op);

}  // namespace pv
