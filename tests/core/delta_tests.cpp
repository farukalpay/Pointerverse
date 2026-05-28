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
    REQUIRE(delta.updates_view().size() == 1);
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

TEST_CASE("temp object can be created and updated before commit") {
    World world{"seed"};
    Verifier verifier;
    verifier.add_builtin("preserve_existing_identity");

    const auto node = world.type_id("Node");
    const auto observation = world.type_id("Observation");

    Delta delta;
    delta.append_create(ObjectCreate{TempObjectId{1}, "A", node, ExistenceState::Alive, {}});
    delta.append_update(ObjectUpdate{ObjectRef{TempObjectId{1}}, observation, std::nullopt});

    const auto overlay = SnapshotOverlay{world.snapshot()}.apply(delta);
    REQUIRE(overlay.has_value());
    REQUIRE(overlay->objects.size() == 1);
    REQUIRE(overlay->objects.front().type == observation);

    const auto result = world.commit(delta, verifier);
    REQUIRE(result.accepted);
    REQUIRE(world.object(world.object_by_name("A")).type == observation);
}

TEST_CASE("temp object can be created and linked in the same delta") {
    World world{"seed"};
    Verifier verifier;
    verifier.add_builtin("reject_dangling_pointer");

    REQUIRE(world.commit(world.object_delta("A", "Node"), verifier).accepted);
    const auto node = world.type_id("Node");
    const auto relation = world.relation_type("causes");

    Delta delta;
    delta.append_create(ObjectCreate{TempObjectId{1}, "B", node, ExistenceState::Alive, {}});
    delta.append_link(PointerCreate{
        ObjectRef{world.object_by_name("A")},
        ObjectRef{TempObjectId{1}},
        relation,
        CausalRole::Structural,
        Weight{0.5},
        "core",
        {}
    });

    const auto result = world.commit(delta, verifier);
    REQUIRE(result.accepted);
    REQUIRE(world.pointers().size() == 1);
    REQUIRE(world.pointer(PointerId{1}).to == world.object_by_name("B"));
}

TEST_CASE("sequential merge rejects unresolved object references") {
    World world{"seed"};
    const auto node = world.type_id("Node");

    Delta delta;
    delta.append_update(ObjectUpdate{ObjectRef{ObjectId{99, 1}}, node, std::nullopt});

    const auto merged = merge_sequential(world.snapshot(), Delta{}, delta);
    REQUIRE_FALSE(merged.has_value());
}

TEST_CASE("sequential merge rejects conflicting type updates inside one delta") {
    World world{"seed"};
    REQUIRE(world.commit(world.object_delta("A", "Node"), Verifier{}).accepted);

    const auto observation = world.type_id("Observation");
    const auto region = world.type_id("Region");
    const auto object = world.object_by_name("A");

    Delta delta;
    delta.append_update(ObjectUpdate{ObjectRef{object}, observation, std::nullopt});
    delta.append_update(ObjectUpdate{ObjectRef{object}, region, std::nullopt});

    const auto merged = merge_sequential(world.snapshot(), Delta{}, delta);
    REQUIRE_FALSE(merged.has_value());
    REQUIRE(merged.error() == DeltaMergeError::ConflictingObjectUpdate);
}

TEST_CASE("sequential merge remaps colliding temp ids from the second delta") {
    World world{"seed"};
    const auto node = world.type_id("Node");
    const auto relation = world.relation_type("causes");

    Delta first;
    first.append_create(ObjectCreate{TempObjectId{1}, "A", node, ExistenceState::Alive, {}});

    Delta second;
    second.append_create(ObjectCreate{TempObjectId{1}, "B", node, ExistenceState::Alive, {}});
    second.append_link(PointerCreate{
        ObjectRef{ObjectId{0, 1}},
        ObjectRef{TempObjectId{1}},
        relation,
        CausalRole::Structural,
        Weight{1.0},
        "core",
        {}
    });

    const auto merged = merge_sequential(world.snapshot(), first, second);
    REQUIRE(merged.has_value());
    const auto creates = merged->creates_view();
    const auto links = merged->links_view();
    REQUIRE(creates.size() == 2);
    REQUIRE(creates[0].temp_id == TempObjectId{1});
    REQUIRE(creates[1].temp_id == TempObjectId{2});
    REQUIRE(std::get<TempObjectId>(links.front().from) == TempObjectId{1});
    REQUIRE(std::get<TempObjectId>(links.front().to) == TempObjectId{2});
}
