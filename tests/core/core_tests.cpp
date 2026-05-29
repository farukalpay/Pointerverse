// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "pv/core/world.hpp"

using namespace pv;

TEST_CASE("objects use stable arena handles with generation checks") {
    World world{"seed"};
    Verifier verifier;

    REQUIRE(world.commit(world.object_delta("A", "Node"), verifier).accepted);
    REQUIRE(world.commit(world.object_delta("B", "Node"), verifier).accepted);

    const auto a = world.object_by_name("A");
    const auto b = world.object_by_name("B");

    REQUIRE(a.index == 0);
    REQUIRE(a.generation == 1);
    REQUIRE(b.index == 1);
    REQUIRE(world.contains(a));
    REQUIRE_FALSE(world.contains(ObjectId{a.index, static_cast<Generation>(a.generation + 1)}));

    const QualifiedObject qa{world.id(), world.epoch(), a};
    REQUIRE(qa == QualifiedObject{WorldId{1}, Epoch{2}, a});
    REQUIRE_FALSE(qa == QualifiedObject{WorldId{1}, Epoch{3}, a});
}

TEST_CASE("existence state changes preserve identity") {
    World world{"seed"};
    Verifier verifier;
    verifier.add_builtin("preserve_existing_identity");

    REQUIRE(world.commit(world.object_delta("A", "Node"), verifier).accepted);
    const auto a = world.object_by_name("A");

    REQUIRE(world.commit(world.existence_delta(a, ExistenceState::Dormant), verifier).accepted);
    REQUIRE(world.object(a).existence == ExistenceState::Dormant);
    REQUIRE(world.contains(a));

    REQUIRE(world.commit(world.existence_delta(a, ExistenceState::Collapsed), verifier).accepted);
    REQUIRE(world.object(a).existence == ExistenceState::Collapsed);

    REQUIRE(world.commit(world.existence_delta(a, ExistenceState::Tombstoned), verifier).accepted);
    REQUIRE(world.object(a).existence == ExistenceState::Tombstoned);
    REQUIRE(world.contains(a));
}

TEST_CASE("pointer edges are typed relation arrows") {
    World world{"seed"};
    Verifier verifier;
    verifier.add_builtin("reject_dangling_pointer");
    verifier.add_builtin("bounded_weight");
    verifier.add_builtin("preserve_relation_type");

    REQUIRE(world.commit(world.object_delta("A", "Node"), verifier).accepted);
    REQUIRE(world.commit(world.object_delta("B", "Node"), verifier).accepted);
    const auto a = world.object_by_name("A");
    const auto b = world.object_by_name("B");

    REQUIRE(world.commit(world.link_delta(a, b, "causes", 0.7, CausalRole::Generative), verifier).accepted);
    REQUIRE(world.pointers().size() == 1);

    const auto& edge = world.pointers().front();
    REQUIRE(edge.from == a);
    REQUIRE(edge.to == b);
    REQUIRE(edge.weight.value == 0.7);
    REQUIRE(edge.causal_role == CausalRole::Generative);
    REQUIRE(edge.active_at(world.epoch()));
}

TEST_CASE("evolution writes deterministic temporal graph edges") {
    World world{"seed"};
    Verifier verifier;
    verifier.add_builtin("reject_dangling_pointer");
    verifier.add_builtin("bounded_weight");
    verifier.add_builtin("preserve_relation_type");
    verifier.add_builtin("no_invalid_epoch_reference");

    REQUIRE(world.commit(world.object_delta("A", "Node"), verifier).accepted);
    REQUIRE(world.commit(world.object_delta("B", "Node"), verifier).accepted);
    REQUIRE(world.commit(world.link_delta(
        world.object_by_name("A"),
        world.object_by_name("B"),
        "causes",
        0.6,
        CausalRole::Generative), verifier).accepted);

    const auto before_pointers = world.pointers().size();
    const auto result = world.evolve(1, verifier);

    REQUIRE(result.completed_steps == 1);
    REQUIRE(world.pointers().size() == before_pointers + 2);

    const auto snapshot = world.snapshot();
    std::size_t evolution_edges = 0;
    for (const auto& pointer : snapshot.pointers) {
        if (pointer.law_domain != "evolution") {
            continue;
        }
        REQUIRE(pointer.from == pointer.to);
        REQUIRE(pointer.causal_role == CausalRole::Transformative);
        REQUIRE(snapshot.relation_name(pointer.relation) == "evolves_to");
        REQUIRE(pointer.born_at == world.epoch());
        evolution_edges += 1;
    }
    REQUIRE(evolution_edges == 2);
}
