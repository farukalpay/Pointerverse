// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

#include "pv/core/world.hpp"
#include "pv/measure/repair_measure.hpp"
#include "pv/storage/repository.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_repo_path(std::string_view name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_measure_repair_" + std::string{name} + "_" + std::to_string(stamp));
    std::filesystem::remove_all(path);
    return path;
}

Transaction object_tx(World& world, std::string name) {
    Transaction tx;
    tx.label = "object " + name;
    tx.delta = world.object_delta(std::move(name), "Node");
    return tx;
}

Transaction link_tx(World& world, double weight) {
    Transaction tx;
    tx.label = "link";
    tx.delta = world.link_delta(world.object_by_name("A"), world.object_by_name("B"), "causes", weight, CausalRole::Structural);
    return tx;
}

}  // namespace

TEST_CASE("repair distance is zero for legal state and positive for illegal state") {
    const auto root = temp_repo_path("distance");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});

    Verifier observe{VerificationMode::Observe};
    observe.add_builtin("bounded_weight");

    const auto clean = repo.commit("main", object_tx(repo.mutable_world("main"), "A"), observe);
    REQUIRE(clean.has_value());
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "B"), observe).has_value());
    const auto illegal = repo.commit("main", link_tx(repo.mutable_world("main"), 1.5), observe);
    REQUIRE(illegal.has_value());
    REQUIRE_FALSE(illegal->violations.empty());

    const auto clean_repair = RepairDistanceMeasure{}.measure(repo, "main", clean->id, observe);
    const auto illegal_repair = RepairDistanceMeasure{}.measure(repo, "main", illegal->id, observe);

    REQUIRE(clean_repair.value == 0);
    REQUIRE(illegal_repair.value > 0);

    std::filesystem::remove_all(root);
}

