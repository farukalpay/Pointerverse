// SPDX-License-Identifier: Apache-2.0
#include "pv/storage/wal.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "pv/hash/hasher.hpp"

namespace pv {
namespace {

std::string bytes_to_hex(std::span<const std::byte> bytes) {
    static constexpr auto digits = std::string_view{"0123456789abcdef"};
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        const auto value = static_cast<unsigned char>(byte);
        out.push_back(digits[value >> 4U]);
        out.push_back(digits[value & 0x0fU]);
    }
    return out;
}

std::vector<std::byte> bytes_from_hex(std::string_view text) {
    auto hex_value = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') {
            return ch - '0';
        }
        if (ch >= 'a' && ch <= 'f') {
            return ch - 'a' + 10;
        }
        if (ch >= 'A' && ch <= 'F') {
            return ch - 'A' + 10;
        }
        return -1;
    };

    if (text.size() % 2 != 0) {
        throw std::runtime_error("invalid WAL hex payload");
    }
    std::vector<std::byte> out;
    out.reserve(text.size() / 2);
    for (std::size_t index = 0; index < text.size(); index += 2) {
        const auto high = hex_value(text[index]);
        const auto low = hex_value(text[index + 1]);
        if (high < 0 || low < 0) {
            throw std::runtime_error("invalid WAL hex payload");
        }
        out.push_back(static_cast<std::byte>((high << 4) | low));
    }
    return out;
}

WalOp op_from_string(std::string_view value) {
    if (value == "BeginCommit") {
        return WalOp::BeginCommit;
    }
    if (value == "PutObject") {
        return WalOp::PutObject;
    }
    if (value == "BindSnapshot") {
        return WalOp::BindSnapshot;
    }
    if (value == "AddCommitNode") {
        return WalOp::AddCommitNode;
    }
    if (value == "UpdateBranchRef") {
        return WalOp::UpdateBranchRef;
    }
    if (value == "EndCommit") {
        return WalOp::EndCommit;
    }
    throw std::runtime_error("unknown WAL operation");
}

}  // namespace

Wal::Wal(std::filesystem::path root) : root_(std::move(root)) {
    std::filesystem::create_directories(root_ / "wal");
}

std::filesystem::path Wal::path() const {
    return root_ / "wal" / "current.log";
}

void Wal::append(WalOp op, std::span<const std::byte> payload) {
    std::filesystem::create_directories(path().parent_path());
    const auto entries = read_all();
    const auto sequence = entries.empty() ? 1 : entries.back().sequence + 1;
    const auto payload_hash = sha256(payload);

    std::ofstream output(path(), std::ios::app);
    if (!output) {
        throw std::runtime_error("cannot append WAL");
    }
    output << sequence << ' ' << to_string(op) << ' ' << to_hex(payload_hash) << ' ' << bytes_to_hex(payload) << '\n';
    output.flush();
    if (!output) {
        throw std::runtime_error("failed appending WAL");
    }
}

std::vector<WalEntry> Wal::read_all() const {
    std::vector<WalEntry> entries;
    std::ifstream input(path());
    if (!input) {
        return entries;
    }

    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        std::istringstream stream(line);
        std::string op;
        std::string hash_text;
        std::string payload_text;
        WalEntry entry;
        stream >> entry.sequence >> op >> hash_text >> payload_text;
        const auto parsed_hash = parse_hash256(hash_text);
        if (!parsed_hash.has_value()) {
            throw std::runtime_error("invalid WAL payload hash");
        }
        entry.op = op_from_string(op);
        entry.payload_hash = *parsed_hash;
        entry.payload = bytes_from_hex(payload_text);
        if (sha256(entry.payload) != entry.payload_hash) {
            throw std::runtime_error("WAL payload hash mismatch");
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

WalRecoveryReport Wal::recover() const {
    WalRecoveryReport report;
    bool in_commit = false;
    for (const auto& entry : read_all()) {
        report.entries_read += 1;
        if (entry.op == WalOp::BeginCommit) {
            in_commit = true;
        } else if (entry.op == WalOp::EndCommit) {
            in_commit = false;
        }
    }
    report.incomplete_commit = in_commit;
    return report;
}

void Wal::truncate() const {
    std::filesystem::create_directories(path().parent_path());
    std::ofstream output(path(), std::ios::trunc);
}

std::string to_string(WalOp op) {
    switch (op) {
    case WalOp::BeginCommit:
        return "BeginCommit";
    case WalOp::PutObject:
        return "PutObject";
    case WalOp::BindSnapshot:
        return "BindSnapshot";
    case WalOp::AddCommitNode:
        return "AddCommitNode";
    case WalOp::UpdateBranchRef:
        return "UpdateBranchRef";
    case WalOp::EndCommit:
        return "EndCommit";
    }
    return "BeginCommit";
}

}  // namespace pv
