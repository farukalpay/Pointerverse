// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "pv/core/world.hpp"

using namespace pv;

TEST_CASE("law verifier rejects dangling pointer snapshots") {
    World world{"seed"};
    Verifier verifier;
    verifier.add_builtin("reject_dangling_pointer");

    auto before = world.snapshot();
    auto after = before;
    after.pointers.push_back(PointerSnapshot{
        PointerId{1},
        ObjectId{7, 1},
        ObjectId{8, 1},
        RelationType{1},
        CausalRole::Structural,
        Weight{0.5},
        Epoch{0},
        std::nullopt,
        "core"
    });
    after.relation_names.emplace(1, "causes");

    const auto result = verifier.check(LawCheckContext{before, Delta{}, after});
    REQUIRE_FALSE(result.accepted);
    REQUIRE(result.violations.front().law == "reject_dangling_pointer");
}

TEST_CASE("bounded weight rejects invalid relation magnitude") {
    World world{"seed"};
    Verifier verifier;
    verifier.add_builtin("bounded_weight");

    REQUIRE(world.commit(world.object_delta("A", "Node"), verifier).accepted);
    REQUIRE(world.commit(world.object_delta("B", "Node"), verifier).accepted);

    const auto result = world.commit(
        world.link_delta(world.object_by_name("A"), world.object_by_name("B"), "causes", -0.1, CausalRole::Structural),
        verifier);

    REQUIRE_FALSE(result.accepted);
    REQUIRE(result.violations.front().law == "bounded_weight");
}

TEST_CASE("relation type and epoch laws are measurement backed") {
    World world{"seed"};
    Verifier verifier;
    verifier.add_builtin("preserve_relation_type");
    verifier.add_builtin("no_invalid_epoch_reference");

    const auto before = world.snapshot();
    auto after = before;
    after.epoch = Epoch{1};
    after.pointers.push_back(PointerSnapshot{
        PointerId{1},
        ObjectId{0, 1},
        ObjectId{0, 1},
        RelationType{},
        CausalRole::Structural,
        Weight{0.5},
        Epoch{2},
        std::nullopt,
        "core"
    });

    const auto result = verifier.check(LawCheckContext{before, Delta{}, after});
    REQUIRE_FALSE(result.accepted);
    REQUIRE(result.violations.size() == 2);
    REQUIRE(result.statuses[0].magnitude >= 1.0);
}
