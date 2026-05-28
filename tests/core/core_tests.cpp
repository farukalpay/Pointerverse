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
