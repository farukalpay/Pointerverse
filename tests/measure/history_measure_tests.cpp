// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

#include "pv/core/world.hpp"
#include "pv/measure/history_measure.hpp"
#include "pv/storage/repository.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_repo_path(std::string_view name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_measure_history_" + std::string{name} + "_" + std::to_string(stamp));
    std::filesystem::remove_all(path);
    return path;
}

Transaction object_tx(World& world, std::string name) {
    Transaction tx;
    tx.label = "object " + name;
    tx.delta = world.object_delta(std::move(name), "Node");
    return tx;
}

Transaction link_tx(World& world) {
    Transaction tx;
    tx.label = "link";
    tx.delta = world.link_delta(world.object_by_name("A"), world.object_by_name("B"), "causes", 1.0, CausalRole::Structural);
    return tx;
}

}  // namespace

TEST_CASE("history surprise decreases after repeated pattern") {
    const auto root = temp_repo_path("repeat");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "A"), Verifier{}).has_value());
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "B"), Verifier{}).has_value());
    const auto first = repo.commit("main", link_tx(repo.mutable_world("main")), Verifier{});
    const auto second = repo.commit("main", link_tx(repo.mutable_world("main")), Verifier{});
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());

    const auto first_surprise = HistorySurpriseMeasure{}.measure(repo, "main", first->id).value;
    const auto second_surprise = HistorySurpriseMeasure{}.measure(repo, "main", second->id).value;

    REQUIRE(second_surprise <= first_surprise);

    std::filesystem::remove_all(root);
}

