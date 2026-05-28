// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

#include "pv/storage/wal.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_wal_path() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_wal_" + std::to_string(stamp));
    std::filesystem::remove_all(path);
    return path;
}

std::vector<std::byte> payload(std::string_view text) {
    std::vector<std::byte> out;
    for (const auto ch : text) {
        out.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }
    return out;
}

}  // namespace

TEST_CASE("wal reports incomplete commit without end entry") {
    const auto root = temp_wal_path();
    Wal wal{root};
    const auto begin = payload("begin");
    wal.append(WalOp::BeginCommit, begin);
    wal.append(WalOp::PutObject, payload("object"));

    const auto report = wal.recover();
    REQUIRE(report.entries_read == 2);
    REQUIRE(report.incomplete_commit);
    std::filesystem::remove_all(root);
}

TEST_CASE("wal complete commit recovers cleanly") {
    const auto root = temp_wal_path();
    Wal wal{root};
    wal.append(WalOp::BeginCommit, payload("begin"));
    wal.append(WalOp::EndCommit, payload("end"));

    const auto report = wal.recover();
    REQUIRE(report.entries_read == 2);
    REQUIRE_FALSE(report.incomplete_commit);
    std::filesystem::remove_all(root);
}
