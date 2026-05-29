// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

#include "pv/storage/content_store.hpp"
#include "pv/storage/pack_store.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_repo_path(std::string_view name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_pack_" + std::string{name} + "_" + std::to_string(stamp));
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

TEST_CASE("content store reads packed objects after loose compaction") {
    const auto root = temp_repo_path("read");
    ContentStore store{root};
    const auto payload = bytes("packed reality");
    const auto id = store.put_bytes(payload);
    REQUIRE(std::filesystem::exists(store.object_path(id)));

    const auto report = PackedContentStore{root}.compact_loose_objects();
    REQUIRE(report.packed_objects == 1);
    REQUIRE_FALSE(std::filesystem::exists(store.object_path(id)));
    REQUIRE(store.contains(id));
    REQUIRE(store.get_bytes(id) == payload);
    std::filesystem::remove_all(root);
}

TEST_CASE("packed content store rejects corrupted packed bytes") {
    const auto root = temp_repo_path("corrupt");
    ContentStore store{root};
    const auto id = store.put_bytes(bytes("clean"));
    (void)PackedContentStore{root}.compact_loose_objects();

    auto pack_path = root / "packs" / "pack-000001.pvp";
    std::fstream pack(pack_path, std::ios::binary | std::ios::in | std::ios::out);
    REQUIRE(pack.good());
    pack.seekp(-1, std::ios::end);
    pack.put('x');
    pack.close();

    REQUIRE_THROWS(store.get_bytes(id));
    std::filesystem::remove_all(root);
}
