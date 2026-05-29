// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

#include "pv/storage/repository.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_repo_path(std::string_view name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_repo_" + std::string{name} + "_" + std::to_string(stamp));
    std::filesystem::remove_all(path);
    return path;
}

Transaction object_tx(World& world, std::string name, std::string type) {
    Transaction tx;
    tx.label = "object " + name;
    tx.delta = world.object_delta(std::move(name), type);
    return tx;
}

Transaction type_update_tx(World& world, ObjectId object, std::string type) {
    Transaction tx;
    tx.label = "type update";
    tx.delta.append_update(ObjectUpdate{ObjectRef{object}, world.type_id(type), std::nullopt});
    return tx;
}

}  // namespace

TEST_CASE("repository reopens with same branch head and snapshot hash") {
    const auto root = temp_repo_path("reopen");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "A", "Node"), Verifier{})->accepted);

    const auto before_ref = repo.list_branches().front();
    const auto reopened = Repository::open(root);
    const auto after_ref = reopened.list_branches().front();

    REQUIRE(after_ref.name == "main");
    REQUIRE(after_ref.head == before_ref.head);
    REQUIRE(after_ref.snapshot == before_ref.snapshot);
    REQUIRE(after_ref.epoch == before_ref.epoch);
    REQUIRE(reopened.history("main").back().id == before_ref.head);
    std::filesystem::remove_all(root);
}

TEST_CASE("repository persists rejected commits without advancing branch head") {
    const auto root = temp_repo_path("rejected");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});

    Verifier verifier;
    verifier.add_builtin("bounded_weight");
    verifier.add_builtin("reject_dangling_pointer");
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "A", "Node"), verifier)->accepted);
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "B", "Node"), verifier)->accepted);
    const auto head = repo.list_branches().front().head;

    Transaction tx;
    tx.label = "invalid weight";
    tx.delta = repo.mutable_world("main").link_delta(
        repo.world("main").object_by_name("A"),
        repo.world("main").object_by_name("B"),
        "causes",
        2.0,
        CausalRole::Structural);
    const auto rejected = repo.commit("main", tx, verifier);
    REQUIRE(rejected.has_value());
    REQUIRE_FALSE(rejected->accepted);
    REQUIRE(repo.list_branches().front().head == head);

    const auto reopened = Repository::open(root);
    REQUIRE(reopened.list_branches().front().head == head);
    REQUIRE(reopened.history("main").size() == 4);
    REQUIRE_FALSE(reopened.history("main").back().accepted);
    std::filesystem::remove_all(root);
}

TEST_CASE("repository branch fork and compare survive reopen") {
    const auto root = temp_repo_path("fork");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "A", "Node"), Verifier{})->accepted);
    (void)repo.fork("main", "experiment/a");

    const auto main_object = repo.world("main").object_by_name("A");
    const auto experiment_object = repo.world("experiment/a").object_by_name("A");
    REQUIRE(repo.commit("main", type_update_tx(repo.mutable_world("main"), main_object, "Region"), Verifier{})->accepted);
    REQUIRE(repo.commit("experiment/a", type_update_tx(repo.mutable_world("experiment/a"), experiment_object, "Archive"), Verifier{})->accepted);

    const auto reopened = Repository::open(root);
    const auto analysis = reopened.analyze_merge("main", "experiment/a");
    REQUIRE(analysis.status == MergeStatus::Conflict);
    REQUIRE(analysis.object_conflicts.size() == 1);
    REQUIRE(analysis.object_conflicts.front().reason == "object type diverged");

    // Each branch added one commit past the fork: those are the first causally
    // relevant commits on each side, and they are distinct.
    REQUIRE(analysis.left_divergence.commit.has_value());
    REQUIRE(analysis.right_divergence.commit.has_value());
    REQUIRE(analysis.left_divergence.commit != analysis.right_divergence.commit);
    REQUIRE_FALSE(analysis.left_divergence.label.empty());
    std::filesystem::remove_all(root);
}
