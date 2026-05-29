// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

#include "pv/storage/repository.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_repo_path() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_lazy_" + std::to_string(stamp));
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

TEST_CASE("opening one hundred branches does not materialize all worlds") {
    const auto root = temp_repo_path();
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "A", "Node"), Verifier{})->accepted);
    for (std::size_t index = 0; index < 99; ++index) {
        (void)repo.fork("main", "branch/" + std::to_string(index));
    }

    const auto reopened = Repository::open(root);

    REQUIRE(reopened.list_branches().size() == 100);
    REQUIRE(reopened.materialized_branch_count() == 0);
    REQUIRE(reopened.history("main").size() == 2);
    REQUIRE(reopened.materialized_branch_count() == 0);
    (void)reopened.world("branch/42");
    REQUIRE(reopened.materialized_branch_count() == 1);
    std::filesystem::remove_all(root);
}

