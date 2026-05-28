// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "pv/core/world.hpp"
#include "pv/core/world_index.hpp"
#include "pv/kernel/merkle.hpp"

using namespace pv;

TEST_CASE("world index matches snapshot lookup expectations") {
    World world{"kernel"};
    Delta delta;
    delta.append_create(ObjectCreate{TempObjectId{1}, "A", world.type_id("Node"), ExistenceState::Alive, {Attribute{"role", string_value("root")}}});
    delta.append_create(ObjectCreate{TempObjectId{2}, "B", world.type_id("Node"), ExistenceState::Alive, {}});
    delta.append_link(PointerCreate{ObjectRef{TempObjectId{1}}, ObjectRef{TempObjectId{2}}, world.relation_type("causes"), CausalRole::Structural, Weight{1.0}, "core", {}});
    REQUIRE(world.commit(delta, Verifier{}).accepted);

    const auto snapshot = world.snapshot();
    WorldIndex index;
    index.rebuild(snapshot);
    REQUIRE(index.objects_by_type(world.type_id("Node")).size() == 2);
    REQUIRE(index.object_by_name("A") == world.object_by_name("A"));
    REQUIRE(index.outgoing(world.object_by_name("A")).size() == 1);
    REQUIRE(index.incoming(world.object_by_name("B")).size() == 1);
    REQUIRE(index.relation(world.relation_type("causes")).size() == 1);
    REQUIRE(index.objects_with_attribute("role").size() == 1);
}

TEST_CASE("world root is stable under snapshot insertion order") {
    World world{"kernel"};
    REQUIRE(world.commit(world.object_delta("A", "Node"), Verifier{}).accepted);
    REQUIRE(world.commit(world.object_delta("B", "Node"), Verifier{}).accepted);
    REQUIRE(world.commit(world.link_delta(world.object_by_name("A"), world.object_by_name("B"), "causes", 1.0, CausalRole::Structural), Verifier{}).accepted);

    auto left = world.snapshot();
    auto right = left;
    std::ranges::reverse(right.objects);
    std::ranges::reverse(right.pointers);
    std::ranges::reverse(right.facts);

    REQUIRE(compute_world_root(left).root == compute_world_root(right).root);
}
