// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <utility>

#include "pv/sentinel/boot_gate.hpp"
#include "pv/storage/ref_store.hpp"
#include "pv/storage/repository.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_repo_path(std::string_view name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_sentinel_boot_" + std::string{name} + "_" + std::to_string(stamp));
    std::filesystem::remove_all(path);
    return path;
}

Transaction object_tx(World& world, std::string name, std::string type) {
    Transaction tx;
    tx.label = "object " + name;
    tx.delta = world.object_delta(std::move(name), type);
    return tx;
}

Hash256 fault_hash() {
    Hash256 hash;
    hash.value[0] = std::byte{0x42};
    return hash;
}

}  // namespace

TEST_CASE("sentinel boot gate reaches ready for a clean repository") {
    const auto root = temp_repo_path("clean");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "A", "Node"), Verifier{})->accepted);

    const auto result = run_boot_gate(root);

    REQUIRE(result.ok);
    REQUIRE(result.failed_at == BootStage::Ready);
    REQUIRE_FALSE(empty(result.measurement.root));
    REQUIRE(std::filesystem::exists(root / "sentinel" / "last_boot"));

    BootGateResult opened_result;
    const auto opened = open_repository_with_sentinel(root, &opened_result);
    REQUIRE(opened_result.ok);
    REQUIRE(opened.has_branch("main"));
    std::filesystem::remove_all(root);
}

TEST_CASE("sentinel boot gate fails at manifest when repository is missing") {
    const auto root = temp_repo_path("missing_manifest");
    std::filesystem::create_directories(root);

    const auto result = run_boot_gate(root);

    REQUIRE_FALSE(result.ok);
    REQUIRE(result.failed_at == BootStage::Manifest);
    std::filesystem::remove_all(root);
}

TEST_CASE("sentinel boot gate fails at object store for corrupted blobs") {
    const auto root = temp_repo_path("corrupt_object");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "A", "Node"), Verifier{})->accepted);

    const auto head = repo.list_branches().front().head;
    std::ofstream output(repo.objects().object_path(head.value), std::ios::binary | std::ios::trunc);
    output << "corrupt";
    output.close();

    const auto result = run_boot_gate(root);

    REQUIRE_FALSE(result.ok);
    REQUIRE(result.failed_at == BootStage::ObjectStore);
    std::filesystem::remove_all(root);
}

TEST_CASE("sentinel boot gate fails at branch refs for missing ref targets") {
    const auto root = temp_repo_path("bad_ref");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    auto ref = *repo.refs().read_branch("main");
    ref.head = CommitId{fault_hash()};
    repo.refs().update_branch(ref);

    const auto result = run_boot_gate(root);

    REQUIRE_FALSE(result.ok);
    REQUIRE(result.failed_at == BootStage::BranchRefs);
    std::filesystem::remove_all(root);
}

TEST_CASE("sentinel boot gate fails at snapshot load for stale branch snapshots") {
    const auto root = temp_repo_path("stale_snapshot");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    const auto genesis_snapshot = repo.list_branches().front().snapshot;
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "A", "Node"), Verifier{})->accepted);

    auto ref = *repo.refs().read_branch("main");
    ref.snapshot = genesis_snapshot;
    repo.refs().update_branch(ref);

    const auto result = run_boot_gate(root);

    REQUIRE_FALSE(result.ok);
    REQUIRE(result.failed_at == BootStage::SnapshotLoad);
    std::filesystem::remove_all(root);
}
