// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

#include "pv/core/world.hpp"
#include "pv/measure/structural_measure.hpp"
#include "pv/storage/repository.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_repo_path(std::string_view name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_measure_structural_" + std::string{name} + "_" + std::to_string(stamp));
    std::filesystem::remove_all(path);
    return path;
}

Transaction object_tx(World& world, std::string name) {
    Transaction tx;
    tx.label = "object " + name;
    tx.delta = world.object_delta(std::move(name), "Node");
    return tx;
}

Transaction link_tx(World& world, std::string from, std::string to) {
    Transaction tx;
    tx.label = "link " + from + " " + to;
    tx.delta = world.link_delta(world.object_by_name(from), world.object_by_name(to), "causes", 1.0, CausalRole::Structural);
    return tx;
}

Transaction assert_object_tx(World& world, std::string name) {
    Transaction tx;
    tx.label = "assert " + name;
    tx.delta.append_assert_object(ObjectRef{world.object_by_name(name)});
    return tx;
}

}  // namespace

TEST_CASE("structural risk increases when causal cone grows and historical commit is stable") {
    const auto root = temp_repo_path("cone");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "A"), Verifier{}).has_value());
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "B"), Verifier{}).has_value());
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "C"), Verifier{}).has_value());
    REQUIRE(repo.commit("main", link_tx(repo.mutable_world("main"), "A", "B"), Verifier{}).has_value());
    const auto before_growth = repo.commit("main", assert_object_tx(repo.mutable_world("main"), "A"), Verifier{});
    REQUIRE(before_growth.has_value());
    const auto measured_before = StructuralRiskMeasure{}.measure(repo, "main", before_growth->id).value;

    REQUIRE(repo.commit("main", link_tx(repo.mutable_world("main"), "B", "C"), Verifier{}).has_value());
    const auto after_growth = repo.commit("main", assert_object_tx(repo.mutable_world("main"), "A"), Verifier{});
    REQUIRE(after_growth.has_value());
    const auto measured_after = StructuralRiskMeasure{}.measure(repo, "main", after_growth->id).value;
    const auto historical_again = StructuralRiskMeasure{}.measure(repo, "main", before_growth->id).value;

    REQUIRE(measured_after > measured_before);
    REQUIRE(historical_again == measured_before);

    std::filesystem::remove_all(root);
}

