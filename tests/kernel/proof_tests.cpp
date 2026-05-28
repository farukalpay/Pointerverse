// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

#include "pv/hash/canonical.hpp"
#include "pv/kernel/proof.hpp"
#include "pv/runtime/world_store.hpp"
#include "pv/storage/integrity.hpp"
#include "pv/storage/repository.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_repo_path(std::string_view name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_proof_" + std::string{name} + "_" + std::to_string(stamp));
    std::filesystem::remove_all(path);
    return path;
}

}  // namespace

TEST_CASE("accepted runtime commits carry proof hashes and roots") {
    WorldStore store;
    const auto main = store.create_branch("main", World{"kernel"});
    Transaction tx;
    tx.label = "object A";
    tx.delta = store.mutable_world(main).object_delta("A", "Node");

    const auto record = store.commit(main, tx, Verifier{});
    REQUIRE(record.has_value());
    REQUIRE(record->accepted);
    REQUIRE(record->proof.has_value());
    REQUIRE_FALSE(empty(record->execution_plan_hash));
    REQUIRE_FALSE(empty(record->proof_hash));
    REQUIRE(record->proof_hash == hash_commit_proof(*record->proof));
    REQUIRE(record->read_set_hash == record->proof->read_set_root);
    REQUIRE(record->write_set_hash == record->proof->write_set_root);
}

TEST_CASE("repository fsck validates stored commit proofs") {
    const auto root = temp_repo_path("fsck");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"kernel"});

    Transaction tx;
    tx.label = "object A";
    tx.delta = repo.mutable_world("main").object_delta("A", "Node");
    REQUIRE(repo.commit("main", tx, Verifier{})->accepted);

    const auto report = IntegrityChecker{}.check_repository(repo);
    REQUIRE(report.clean());
    REQUIRE(report.commits_checked == 2);
    std::filesystem::remove_all(root);
}
