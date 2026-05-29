// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

#include "pv/query/query.hpp"
#include "pv/storage/repository.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_repo_path() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_repo_query_" + std::to_string(stamp));
    std::filesystem::remove_all(path);
    return path;
}

Transaction object_tx(World& world, std::string name, std::string type) {
    Transaction tx;
    tx.label = "object " + name;
    tx.delta = world.object_delta(std::move(name), type);
    return tx;
}

Transaction link_tx(World& world, std::string from, std::string to, std::string relation) {
    Transaction tx;
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

TEST_CASE("repository query engine uses persistent object relation and event indexes") {
    const auto root = temp_repo_path();
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"audit"});
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "Agent0", "Agent"), Verifier{})->accepted);
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "FileA", "File"), Verifier{})->accepted);
    REQUIRE(repo.commit("main", link_tx(repo.mutable_world("main"), "Agent0", "FileA", "modifies"), Verifier{})->accepted);

    const auto reopened = Repository::open(root);
    const RepositoryQueryEngine query;

    const auto agents = query.objects_by_type(reopened, "main", "Agent");
    REQUIRE(agents.objects.size() == 1);
    REQUIRE(reopened.materialized_branch_count() == 0);

    const auto by_name = query.objects_by_name(reopened, "main", "Agent0");
    REQUIRE(by_name.objects == agents.objects);

    const auto modifies = query.links_by_relation(reopened, "main", "modifies");
    REQUIRE(modifies.pointers.size() == 1);

    const auto touches = query.commits_touching_object(reopened, "main", agents.objects.front());
    REQUIRE(touches.commits.size() >= 2);

    const auto events = query.events_by_name(reopened, "main", "pointer.create");
    REQUIRE(events.commits.size() == 1);
    REQUIRE(reopened.materialized_branch_count() == 0);
    std::filesystem::remove_all(root);
}

