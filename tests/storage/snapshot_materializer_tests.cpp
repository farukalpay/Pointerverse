// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

#include "pv/storage/repository.hpp"
#include "pv/storage/object_codec.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_repo_path(std::string_view name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_materializer_" + std::string{name} + "_" + std::to_string(stamp));
    std::filesystem::remove_all(path);
    return path;
}

Transaction object_tx(World& world, std::string name) {
    Transaction tx;
    tx.label = "object " + name;
    tx.delta = world.object_delta(std::move(name), "Node");
    return tx;
}

}  // namespace

TEST_CASE("snapshot materializer uses nearest checkpoint instead of full history") {
    const auto root = temp_repo_path("chain");
    RepositoryOptions options;
    options.checkpoint_policy.every_n_commits = 8;
    auto repo = Repository::init(root, options);
    (void)repo.create_branch("main", World{"seed"});
    for (std::size_t index = 0; index < 32; ++index) {
        REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "O" + std::to_string(index)), Verifier{})->accepted);
    }

    const auto history = repo.history("main");
    const auto early = history.at(2).id;
    const auto early_stored = repo.objects().get_canonical<StoredCommit>(early.value);
    std::filesystem::remove(repo.objects().object_path(early_stored.delta_object));

    const auto reopened = Repository::open(root, options);
    const auto snapshot = reopened.backend().snapshot("main");
    REQUIRE(snapshot.objects.size() == 32);
    REQUIRE(snapshot.canonical_hash() == reopened.list_branches().front().snapshot);
    std::filesystem::remove_all(root);
}

TEST_CASE("checkpoint policy records distances and fork checkpoints") {
    const auto root = temp_repo_path("policy");
    RepositoryOptions options;
    options.checkpoint_policy.every_n_commits = 3;
    auto repo = Repository::init(root, options);
    (void)repo.create_branch("main", World{"seed"});
    for (std::size_t index = 0; index < 4; ++index) {
        REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "M" + std::to_string(index)), Verifier{})->accepted);
    }
    const auto history = repo.history("main");
    REQUIRE(history[0].checkpoint_distance == 0);
    REQUIRE(history[1].checkpoint_distance == 1);
    REQUIRE(history[2].checkpoint_distance == 2);
    REQUIRE(history[3].checkpoint_distance == 0);
    REQUIRE_FALSE(empty(history[3].checkpoint_snapshot_object));

    (void)repo.fork("main", "feature");
    REQUIRE(repo.commit("feature", object_tx(repo.mutable_world("feature"), "F"), Verifier{})->accepted);
    REQUIRE(repo.history("feature").back().checkpoint_distance == 0);
    REQUIRE_FALSE(empty(repo.history("feature").back().checkpoint_snapshot_object));
    std::filesystem::remove_all(root);
}

TEST_CASE("snapshot-level delta apply reports invalid references") {
    World world{"seed"};
    auto delta = world.object_delta("A", "Node");
    const auto applied = apply_delta_to_snapshot(world.snapshot(), delta);
    REQUIRE(applied.has_value());
    REQUIRE(applied->objects.size() == 1);

    Delta invalid;
    invalid.append_update(ObjectUpdate{ObjectRef{ObjectId{99, 1}}, world.type_id("Node"), std::nullopt});
    REQUIRE_FALSE(apply_delta_to_snapshot(world.snapshot(), invalid).has_value());
}
