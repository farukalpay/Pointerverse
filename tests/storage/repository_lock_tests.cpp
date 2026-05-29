// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

#include "pv/storage/lock_file.hpp"
#include "pv/storage/repository.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_repo_path() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_lock_" + std::to_string(stamp));
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

TEST_CASE("two concurrent repository writers cannot commit simultaneously") {
    const auto root = temp_repo_path();
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});

    RepositoryWriteLock held{root};

    REQUIRE_THROWS(repo.commit("main", object_tx(repo.mutable_world("main"), "A", "Node"), Verifier{}));
    std::filesystem::remove_all(root);
}

