// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

#include "pv/storage/repository.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_repo_path(std::string_view name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_gc_" + std::string{name} + "_" + std::to_string(stamp));
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

Transaction object_tx(World& world) {
    Transaction tx;
    tx.label = "object A";
    tx.delta = world.object_delta("A", "Node");
    return tx;
}

}  // namespace

TEST_CASE("repository gc marks quarantines and prunes unreachable loose objects") {
    const auto root = temp_repo_path("quarantine");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main")), Verifier{})->accepted);

    const auto orphan = repo.objects().put_bytes(bytes("orphan"));
    auto report = repo.gc_mark();
    REQUIRE(report.reachable_objects > 0);
    REQUIRE(report.unreachable_objects == 1);

    report = repo.gc_quarantine();
    REQUIRE(report.quarantined_objects == 1);
    REQUIRE_FALSE(std::filesystem::exists(repo.objects().object_path(orphan)));

    repo.gc_prune();
    REQUIRE_FALSE(std::filesystem::exists(root / "quarantine"));
    std::filesystem::remove_all(root);
}
