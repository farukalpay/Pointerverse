// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

#include "pv/storage/integrity.hpp"
#include "pv/storage/repository.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_repo_path(std::string_view name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_fsck_" + std::string{name} + "_" + std::to_string(stamp));
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

TEST_CASE("repository fsck reports clean repository") {
    const auto root = temp_repo_path("clean");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "A", "Node"), Verifier{})->accepted);

    const auto report = IntegrityChecker{}.check_repository(repo);
    REQUIRE(report.clean());
    REQUIRE(report.commits_checked == 2);
    REQUIRE(report.branch_refs_checked == 1);
    std::filesystem::remove_all(root);
}

TEST_CASE("repository fsck detects corrupted object blob") {
    const auto root = temp_repo_path("corrupt");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "A", "Node"), Verifier{})->accepted);

    const auto head = repo.list_branches().front().head;
    std::ofstream output(repo.objects().object_path(head.value), std::ios::binary | std::ios::trunc);
    output << "corrupt";
    output.close();

    const auto report = IntegrityChecker{}.check_repository(repo);
    REQUIRE_FALSE(report.clean());
    REQUIRE_FALSE(report.errors.empty());
    std::filesystem::remove_all(root);
}

TEST_CASE("repository fsck detects missing parent commit object") {
    const auto root = temp_repo_path("missing_parent");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "A", "Node"), Verifier{})->accepted);
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "B", "Node"), Verifier{})->accepted);

    const auto parent = repo.history("main")[1].id;
    std::filesystem::remove(repo.objects().object_path(parent.value));

    const auto report = IntegrityChecker{}.check_repository(repo);
    REQUIRE_FALSE(report.clean());
    std::filesystem::remove_all(root);
}
