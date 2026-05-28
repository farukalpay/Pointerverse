// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "pv/category/morphism.hpp"
#include "pv/core/world.hpp"

using namespace pv;

TEST_CASE("rejected deltas leave world state unchanged and write trace") {
    World world{"seed"};
    Verifier verifier;
    verifier.add_builtin("bounded_weight");
    verifier.add_builtin("reject_dangling_pointer");

    REQUIRE(world.commit(world.object_delta("A", "Node"), verifier).accepted);
    REQUIRE(world.commit(world.object_delta("B", "Node"), verifier).accepted);
    const auto before_epoch = world.epoch();
    const auto before_hash = world.hash();

    const auto rejected = world.commit(
        world.link_delta(world.object_by_name("A"), world.object_by_name("B"), "causes", 1.5, CausalRole::Structural),
        verifier);

    REQUIRE_FALSE(rejected.accepted);
    REQUIRE(world.epoch() == before_epoch);
    REQUIRE(world.hash() == before_hash);
    REQUIRE(world.pointers().empty());
    REQUIRE_FALSE(world.trace().empty());
    REQUIRE(world.trace().events().back().event == "law.check");
}

TEST_CASE("morphisms produce deltas before world commit") {
    World world{"seed"};
    Verifier verifier;
    verifier.add_builtin("preserve_existing_identity");

    REQUIRE(world.commit(world.object_delta("A", "Node"), verifier).accepted);
    const auto node = world.type_id("Node");
    const auto observation = world.type_id("Observation");
    DefinedMorphism readout{"Readout", MorphismSignature{node, observation}};

    const auto delta = readout.apply(world.snapshot(), Selection{{world.object_by_name("A")}, {}});
    REQUIRE(delta.updates.size() == 1);
    REQUIRE(world.type_name(world.object(world.object_by_name("A")).type) == "Node");

    const auto result = world.commit(delta, verifier);
    REQUIRE(result.accepted);
    REQUIRE(world.type_name(world.object(world.object_by_name("A")).type) == "Observation");
}

TEST_CASE("same seed and same deltas produce same world hash") {
    Verifier verifier;
    verifier.add_builtin("reject_dangling_pointer");
    verifier.add_builtin("bounded_weight");

    World left{"seed"};
    World right{"seed"};

    REQUIRE(left.commit(left.object_delta("A", "Node"), verifier).accepted);
    REQUIRE(right.commit(right.object_delta("A", "Node"), verifier).accepted);
    REQUIRE(left.commit(left.object_delta("B", "Node"), verifier).accepted);
    REQUIRE(right.commit(right.object_delta("B", "Node"), verifier).accepted);

    REQUIRE(left.commit(left.link_delta(left.object_by_name("A"), left.object_by_name("B"), "causes", 0.4, CausalRole::Structural), verifier).accepted);
    REQUIRE(right.commit(right.link_delta(right.object_by_name("A"), right.object_by_name("B"), "causes", 0.4, CausalRole::Structural), verifier).accepted);

    REQUIRE(left.hash() == right.hash());
}
