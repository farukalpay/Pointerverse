// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

#include "pv/query/query.hpp"
#include "pv/storage/repository.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_repo_path(std::string_view name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_query_" + std::string{name} + "_" + std::to_string(stamp));
    std::filesystem::remove_all(path);
    return path;
}

Transaction object_tx(World& world, std::string name, std::string type) {
    Transaction tx;
    tx.origin = TransactionOrigin::Script;
    tx.label = "object " + name;
    tx.delta = world.object_delta(std::move(name), type);
    return tx;
}

Transaction link_tx(World& world, std::string from, std::string to, std::string relation) {
    Transaction tx;
    tx.origin = TransactionOrigin::Script;
    tx.label = "link " + from + " -> " + to + " : " + relation;
    tx.delta = world.link_delta(
        world.object_by_name(from),
        world.object_by_name(to),
        relation,
        1.0,
        CausalRole::Structural);
    return tx;
}

}  // namespace

TEST_CASE("query engine finds objects links cones and touching commits") {
    const auto root = temp_repo_path("engine");
    auto repository = Repository::init(root);
    (void)repository.create_branch("main", World{"audit"});

    REQUIRE(repository.commit("main", object_tx(repository.mutable_world("main"), "Agent0", "Agent"), Verifier{})->accepted);
    REQUIRE(repository.commit("main", object_tx(repository.mutable_world("main"), "FileA", "File"), Verifier{})->accepted);
    REQUIRE(repository.commit("main", link_tx(repository.mutable_world("main"), "Agent0", "FileA", "modifies"), Verifier{})->accepted);

    const auto snapshot = repository.world("main").snapshot();
    const QueryEngine query;

    const auto agents = query.objects_by_type(snapshot, "Agent");
    REQUIRE(agents.objects.size() == 1);

    const auto modifies = query.links_by_relation(snapshot, "modifies");
    REQUIRE(modifies.pointers.size() == 1);

    const auto cone = query.causal_cone(snapshot, agents.objects.front(), 1, "out");
    REQUIRE(cone.objects.size() == 2);
    REQUIRE(cone.pointers.size() == 1);

    const auto commits = query.commits_touching_object(repository, "main", agents.objects.front());
    REQUIRE(commits.commits.size() >= 2);

    std::filesystem::remove_all(root);
}
