// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <utility>

#include "pv/sentinel/sentinel.hpp"
#include "pv/storage/repository.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_repo_path(std::string_view name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_sentinel_heartbeat_" + std::string{name} + "_" + std::to_string(stamp));
    std::filesystem::remove_all(path);
    return path;
}

Transaction object_tx(World& world, std::string name, std::string type) {
    Transaction tx;
    tx.label = "object " + name;
    tx.delta = world.object_delta(std::move(name), type);
    return tx;
}

}  // namespace

TEST_CASE("sentinel runtime emits healthy synchronous worker heartbeats") {
    const auto root = temp_repo_path("runtime");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "A", "Node"), Verifier{})->accepted);

    SentinelRuntime runtime{root};
    const auto report = runtime.tick();

    REQUIRE(report.clean());
    REQUIRE(report.heartbeats.size() == 3);
    for (const auto& heartbeat : report.heartbeats) {
        REQUIRE(heartbeat.tick == 1);
        REQUIRE(heartbeat.healthy);
        REQUIRE_FALSE(empty(heartbeat.last_measurement));
    }
    std::filesystem::remove_all(root);
}
