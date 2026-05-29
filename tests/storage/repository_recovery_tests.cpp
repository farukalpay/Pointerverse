// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

#include "pv/storage/repository.hpp"
#include "pv/storage/wal.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_repo_path(std::string_view name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_recovery_" + std::string{name} + "_" + std::to_string(stamp));
    std::filesystem::remove_all(path);
    return path;
}

Transaction object_tx(World& world, std::string name, std::string type) {
    Transaction tx;
    tx.label = "object " + name;
    tx.delta = world.object_delta(std::move(name), type);
    return tx;
}

std::vector<std::byte> payload(std::string_view text) {
    std::vector<std::byte> out;
    for (const auto ch : text) {
        out.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }
    return out;
}

}  // namespace

TEST_CASE("repository recovery rolls back prepared but unpublished transaction debris") {
    const auto root = temp_repo_path("prepared");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "A", "Node"), Verifier{})->accepted);

    Wal{root}.append(WalOp::BeginCommit, payload("prepared-only"));
    std::filesystem::create_directories(root / "staging" / "txn");
    {
        std::ofstream tmp(root / "index" / "commits.idx.tmp");
        tmp << "partial";
    }

    const auto reopened = Repository::open(root);

    REQUIRE_FALSE(std::filesystem::exists(root / "staging"));
    REQUIRE_FALSE(std::filesystem::exists(root / "index" / "commits.idx.tmp"));
    REQUIRE_FALSE(Wal{root}.recover().incomplete_commit);
    REQUIRE(reopened.history("main").size() == 2);
    std::filesystem::remove_all(root);
}

TEST_CASE("repository recovery finishes ref-published transaction by rebuilding missing indexes") {
    const auto root = temp_repo_path("missing_index");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "A", "Node"), Verifier{})->accepted);
    const auto head = repo.list_branches().front().head;

    std::filesystem::remove(root / "index" / "commits.idx");
    std::filesystem::remove(root / "index" / "branches.idx");
    std::filesystem::remove(root / "index" / "events.idx");
    std::filesystem::remove(root / "index" / "objects.idx");
    std::filesystem::remove(root / "index" / "relations.idx");

    const auto reopened = Repository::open(root);

    REQUIRE(reopened.list_branches().front().head == head);
    REQUIRE(reopened.history("main").back().id == head);
    REQUIRE(reopened.check_indexes().clean);
    std::filesystem::remove_all(root);
}

TEST_CASE("repository recovery repairs branch ref past history from commit parent chain") {
    const auto root = temp_repo_path("ref_past_history");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "A", "Node"), Verifier{})->accepted);
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "B", "Node"), Verifier{})->accepted);
    const auto head = repo.list_branches().front().head;

    {
        std::ofstream history(root / "history" / "branches" / "main", std::ios::trunc);
        history << to_hex(repo.history("main").front().id.value) << '\n';
    }
    std::filesystem::remove(root / "index" / "branches.idx");

    const auto reopened = Repository::open(root);

    REQUIRE(reopened.history("main").back().id == head);
    REQUIRE(reopened.history("main").size() == 3);
    std::filesystem::remove_all(root);
}

