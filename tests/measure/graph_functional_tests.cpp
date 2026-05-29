// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

#include "pv/core/world.hpp"
#include "pv/measure/graph_functional.hpp"
#include "pv/measure/graph_view.hpp"
#include "pv/storage/repository.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_repo_path(std::string_view name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_graph_functional_" + std::string{name} + "_" + std::to_string(stamp));
    std::filesystem::remove_all(path);
    return path;
}

ObjectId object(std::uint32_t index) {
    return ObjectId{index, 0};
}

PointerId pointer(std::uint64_t value) {
    return PointerId{value};
}

WeightedArc arc(std::uint32_t from, std::uint32_t to, std::uint64_t id, std::uint64_t weight = 1000000) {
    return WeightedArc{object(from), object(to), pointer(id), weight, "causes"};
}

Transaction object_tx(World& world, std::string name) {
    Transaction tx;
    tx.label = "object " + name;
    tx.delta = world.object_delta(std::move(name), "Node");
    return tx;
}

Transaction link_tx(World& world, std::string from, std::string to) {
    Transaction tx;
    tx.label = "link";
    tx.delta = world.link_delta(world.object_by_name(from), world.object_by_name(to), "causes", 1.0, CausalRole::Structural);
    return tx;
}

}  // namespace

TEST_CASE("forward mass is deterministic under arc insertion order changes") {
    WeightedGraphView left;
    left.objects = {object(1), object(2), object(3)};
    left.arcs = {arc(1, 2, 1), arc(2, 3, 2), arc(1, 3, 3)};

    WeightedGraphView right;
    right.objects = {object(1), object(2), object(3)};
    right.arcs = {arc(1, 3, 3), arc(2, 3, 2), arc(1, 2, 1)};

    const std::vector<ObjectId> seeds{object(1)};
    const auto left_result = ForwardConeMass{}.evaluate(left, seeds);
    const auto right_result = ForwardConeMass{}.evaluate(right, seeds);

    REQUIRE(left_result.value == right_result.value);
    REQUIRE(functional_result_hash("forward_cone_mass", left_result)
        == functional_result_hash("forward_cone_mass", right_result));
}

TEST_CASE("same graph with different object insertion order gives same functional result") {
    WeightedGraphView left;
    left.objects = {object(1), object(2), object(3)};
    left.arcs = {arc(1, 2, 1), arc(2, 3, 2)};

    WeightedGraphView right;
    right.objects = {object(3), object(1), object(2)};
    right.arcs = {arc(2, 3, 2), arc(1, 2, 1)};

    const std::vector<ObjectId> seeds{object(1)};
    const auto left_result = PropagatedMass{}.evaluate(left, seeds);
    const auto right_result = PropagatedMass{}.evaluate(right, seeds);

    REQUIRE(functional_result_hash("propagated_mass", left_result)
        == functional_result_hash("propagated_mass", right_result));
}

TEST_CASE("functional result hash is stable after repository reopen") {
    const auto root = temp_repo_path("reopen");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "A"), Verifier{}).has_value());
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "B"), Verifier{}).has_value());
    const auto link = repo.commit("main", link_tx(repo.mutable_world("main"), "A", "B"), Verifier{});
    REQUIRE(link.has_value());

    const auto seed = repo.world("main").object_by_name("A");
    const std::vector<ObjectId> seeds{seed};
    const auto before_graph = weighted_graph_view_for_commit(repo, link->id);
    const auto before = functional_result_hash(
        "forward_cone_mass",
        ForwardConeMass{}.evaluate(before_graph, seeds));

    const auto reopened = Repository::open(root);
    const auto after_graph = weighted_graph_view_for_commit(reopened, link->id);
    const auto after = functional_result_hash(
        "forward_cone_mass",
        ForwardConeMass{}.evaluate(after_graph, seeds));

    REQUIRE(after == before);
    std::filesystem::remove_all(root);
}
