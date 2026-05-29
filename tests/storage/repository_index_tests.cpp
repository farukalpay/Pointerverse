// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

#include "pv/query/query.hpp"
#include "pv/storage/integrity.hpp"
#include "pv/storage/repository.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_repo_path(std::string_view name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_index_" + std::string{name} + "_" + std::to_string(stamp));
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

TEST_CASE("repository history is served from branch and commit indexes") {
    const auto root = temp_repo_path("history");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "A", "Node"), Verifier{})->accepted);
    const auto head = repo.list_branches().front().head;

    std::filesystem::remove(root / "history" / "branches" / "main");
    const auto reopened = Repository::open(root);

    REQUIRE(reopened.materialized_branch_count() == 0);
    REQUIRE(reopened.history("main").back().id == head);
    REQUIRE(reopened.materialized_branch_count() == 0);
    std::filesystem::remove_all(root);
}

TEST_CASE("repository index rebuild recreates identical index hashes") {
    const auto root = temp_repo_path("checksum");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "A", "Node"), Verifier{})->accepted);

    const auto before = repo.check_indexes();
    repo.rebuild_indexes();
    const auto after = repo.check_indexes();

    REQUIRE(after.clean);
    REQUIRE(before.commits_checksum == after.commits_checksum);
    REQUIRE(before.branches_checksum == after.branches_checksum);
    REQUIRE(before.events_checksum == after.events_checksum);
    REQUIRE(before.objects_checksum == after.objects_checksum);
    REQUIRE(before.relations_checksum == after.relations_checksum);
    std::filesystem::remove_all(root);
}

TEST_CASE("repository compact preserves branch heads and fsck passes") {
    const auto root = temp_repo_path("compact");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "A", "Node"), Verifier{})->accepted);
    const auto head = repo.list_branches().front().head;

    repo.compact();

    REQUIRE(repo.list_branches().front().head == head);
    REQUIRE(IntegrityChecker{}.check_repository(repo).clean());
    std::filesystem::remove_all(root);
}

TEST_CASE("repository reopen preserves refs indexes world roots and query results") {
    const auto root = temp_repo_path("reopen_invariant");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "A", "Agent"), Verifier{})->accepted);

    const RepositoryQueryEngine query;
    const auto before_ref = repo.list_branches().front();
    const auto before_check = repo.check_indexes();
    const auto before_root = repo.world("main").snapshot().canonical_hash();
    const auto before_query = query.objects_by_type(repo, "main", "Agent");

    const auto reopened = Repository::open(root);
    const auto after_ref = reopened.list_branches().front();
    const auto after_check = reopened.check_indexes();
    const auto after_root = reopened.world("main").snapshot().canonical_hash();
    const auto after_query = query.objects_by_type(reopened, "main", "Agent");

    REQUIRE(after_ref.head == before_ref.head);
    REQUIRE(after_ref.snapshot == before_ref.snapshot);
    REQUIRE(after_check.commits_checksum == before_check.commits_checksum);
    REQUIRE(after_check.branches_checksum == before_check.branches_checksum);
    REQUIRE(after_root == before_root);
    REQUIRE(after_query.objects == before_query.objects);
    std::filesystem::remove_all(root);
}

