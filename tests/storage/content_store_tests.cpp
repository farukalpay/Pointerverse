// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

#include "pv/storage/content_store.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_repo_path(std::string_view name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_" + std::string{name} + "_" + std::to_string(stamp));
    std::filesystem::remove_all(path);
    return path;
}

std::vector<std::byte> bytes(std::string_view text) {
    std::vector<std::byte> out;
    for (const auto ch : text) {
        out.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }
    return out;
}

}  // namespace

TEST_CASE("content store writes objects by content hash path") {
    const auto root = temp_repo_path("objects");
    ContentStore store{root};
    const auto payload = bytes("persistent reality");
    const auto id = store.put_bytes(payload);

    REQUIRE(store.contains(id));
    REQUIRE(store.get_bytes(id) == payload);
    REQUIRE(store.object_path(id).parent_path().filename() == to_hex(id).substr(0, 2));
    REQUIRE(store.object_path(id).filename() == to_hex(id));
    REQUIRE(store.put_bytes(payload) == id);

    std::filesystem::remove_all(root);
}

TEST_CASE("content store rejects corrupted object bytes") {
    const auto root = temp_repo_path("corrupt");
    ContentStore store{root};
    const auto id = store.put_bytes(bytes("clean"));

    std::ofstream output(store.object_path(id), std::ios::binary | std::ios::trunc);
    output << "dirty";
    output.close();

    REQUIRE_THROWS(store.get_bytes(id));
    std::filesystem::remove_all(root);
}
