// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <utility>

#include "pv/sentinel/fault_injection.hpp"
#include "pv/sentinel/patrol.hpp"
#include "pv/storage/repository.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_repo_path(std::string_view name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_sentinel_patrol_" + std::string{name} + "_" + std::to_string(stamp));
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

TEST_CASE("sentinel patrol reports clean repository state") {
    const auto root = temp_repo_path("clean");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "A", "Node"), Verifier{})->accepted);

    const auto report = patrol_repository(repo);

    REQUIRE(report.clean());
    REQUIRE(report.commits_checked == 2);
    REQUIRE(report.branch_refs_checked == 1);
    std::filesystem::remove_all(root);
}

TEST_CASE("store patrol detects object filename hash mismatch") {
    const auto root = temp_repo_path("store_corrupt");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "A", "Node"), Verifier{})->accepted);

    const auto head = repo.list_branches().front().head;
    std::ofstream output(repo.objects().object_path(head.value), std::ios::binary | std::ios::trunc);
    output << "corrupt";
    output.close();

    const auto report = StorePatrolWorker{}.run(root);

    REQUIRE_FALSE(report.clean());
    REQUIRE(report.store_corruptions == 1);
    std::filesystem::remove_all(root);
}

TEST_CASE("proof patrol detects rewritten proof roots") {
    const auto root = temp_repo_path("proof_flip");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "A", "Node"), Verifier{})->accepted);

    FaultInjectionOptions options;
    options.root = root;
    options.branch = "main";
    options.commit = "HEAD";
    options.confirm_mutates_store = true;
    REQUIRE(flip_proof_fault(options).mutated);

    const auto reopened = Repository::open(root);
    const auto report = patrol_repository(reopened);

    REQUIRE_FALSE(report.clean());
    REQUIRE(report.proof_mismatches > 0);
    std::filesystem::remove_all(root);
}
