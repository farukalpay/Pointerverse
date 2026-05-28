// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "pv/runtime/replayer.hpp"
#include "pv/runtime/world_store.hpp"

using namespace pv;

namespace {

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

TEST_CASE("runtime commit record preserves hashes and parent identity") {
    WorldStore store;
    const auto main = store.create_branch("main", World{"seed"});
    const auto parent = store.branch(main).head;
    const auto before_hash = store.world(main).canonical_hash();

    auto tx = object_tx(store.mutable_world(main), "A", "Node");
    tx.origin = TransactionOrigin::Script;
    tx.morphism_path = {"seed", "create"};
    const auto record = store.commit(main, tx, Verifier{});

    REQUIRE(record.has_value());
    REQUIRE(record->accepted);
    REQUIRE(record->parent == parent);
    REQUIRE(record->parents.size() == 1);
    REQUIRE(record->before_hash == before_hash);
    REQUIRE(record->after_hash == store.world(main).canonical_hash());
    REQUIRE(record->delta_hash == canonical_hash(tx.delta));
    REQUIRE(record->morphism_path_hash == canonical_hash_morphism_path(tx.morphism_path));
    REQUIRE(record->branch == main);
    REQUIRE(record->branch_name == "main");
    REQUIRE(record->before_epoch == Epoch{0});
    REQUIRE(record->after_epoch == Epoch{1});
    REQUIRE(record->id.valid());
}

TEST_CASE("forked branch starts from identical snapshot hash and mutates independently") {
    WorldStore store;
    const auto main = store.create_branch("main", World{"seed"});
    REQUIRE(store.commit(main, object_tx(store.mutable_world(main), "A", "Node"), Verifier{})->accepted);
    const auto main_hash = store.world(main).canonical_hash();

    const auto fork = store.fork_branch(main, "experiment/a");
    REQUIRE(store.world(fork).canonical_hash() == main_hash);

    REQUIRE(store.commit(fork, object_tx(store.mutable_world(fork), "B", "Node"), Verifier{})->accepted);

    REQUIRE(store.world(main).canonical_hash() == main_hash);
    REQUIRE_FALSE(store.world(main).has_object("B"));
    REQUIRE(store.world(fork).has_object("B"));
}

TEST_CASE("branch history forms a parent linked accepted chain") {
    WorldStore store;
    const auto main = store.create_branch("main", World{"seed"});
    REQUIRE(store.commit(main, object_tx(store.mutable_world(main), "A", "Node"), Verifier{})->accepted);
    REQUIRE(store.commit(main, object_tx(store.mutable_world(main), "B", "Node"), Verifier{})->accepted);

    const auto history = store.history(main);
    REQUIRE(history.size() == 3);
    REQUIRE_FALSE(history[0].parent.has_value());
    REQUIRE(history[1].parent == history[0].id);
    REQUIRE(history[2].parent == history[1].id);
    REQUIRE(store.graph().common_ancestor(history[1].id, history[2].id) == history[1].id);
}

TEST_CASE("rejected runtime commit is recorded without advancing branch head") {
    WorldStore store;
    const auto main = store.create_branch("main", World{"seed"});
    Verifier verifier;
    verifier.add_builtin("bounded_weight");
    verifier.add_builtin("reject_dangling_pointer");

    REQUIRE(store.commit(main, object_tx(store.mutable_world(main), "A", "Node"), verifier)->accepted);
    REQUIRE(store.commit(main, object_tx(store.mutable_world(main), "B", "Node"), verifier)->accepted);
    const auto head = store.branch(main).head;

    Transaction tx;
    tx.label = "invalid weight";
    tx.delta = store.mutable_world(main).link_delta(
        store.world(main).object_by_name("A"),
        store.world(main).object_by_name("B"),
        "causes",
        2.0,
        CausalRole::Structural);
    const auto record = store.commit(main, tx, verifier);

    REQUIRE(record.has_value());
    REQUIRE_FALSE(record->accepted);
    REQUIRE(record->parent == head);
    REQUIRE(store.branch(main).head == head);
    REQUIRE(store.history(main).size() == 4);
}

TEST_CASE("runtime replay reconstructs commit sequence and final snapshot") {
    Verifier verifier;
    World source{"seed"};
    REQUIRE(source.commit(source.object_delta("A", "Node"), verifier).accepted);
    REQUIRE(source.commit(source.object_delta("B", "Node"), verifier).accepted);
    REQUIRE(source.commit(source.link_delta(
        source.object_by_name("A"),
        source.object_by_name("B"),
        "causes",
        0.7,
        CausalRole::Structural), verifier).accepted);
    REQUIRE(source.evolve(1, verifier).rejected_steps == 0);

    WorldStore store;
    const auto main = store.create_branch("main", World{"seed"});
    const auto result = RuntimeReplayer{}.replay_into(store, main, source.trace().to_jsonl(), verifier);

    REQUIRE(result.errors.empty());
    REQUIRE(result.commits_replayed == 4);
    REQUIRE(result.final_hash == source.canonical_hash());
    REQUIRE(store.world(main).snapshot().canonical_hash() == source.snapshot().canonical_hash());
}

TEST_CASE("branch comparison detects object type divergence") {
    WorldStore store;
    const auto main = store.create_branch("main", World{"seed"});
    REQUIRE(store.commit(main, object_tx(store.mutable_world(main), "A", "Node"), Verifier{})->accepted);
    const auto experiment = store.fork_branch(main, "experiment/a");

    const auto main_object = store.world(main).object_by_name("A");
    const auto experiment_object = store.world(experiment).object_by_name("A");
    REQUIRE(store.commit(main, type_update_tx(store.mutable_world(main), main_object, "Region"), Verifier{})->accepted);
    REQUIRE(store.commit(experiment, type_update_tx(store.mutable_world(experiment), experiment_object, "Archive"), Verifier{})->accepted);

    const auto analysis = store.analyze_merge(main, experiment);
    REQUIRE(analysis.status == MergeStatus::Conflict);
    REQUIRE(analysis.common_ancestor.has_value());
    REQUIRE(analysis.object_conflicts.size() == 1);
    REQUIRE(analysis.object_conflicts.front().reason == "object type diverged");
}
