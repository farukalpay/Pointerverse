// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "pv/core/world.hpp"
#include "pv/kernel/execution_plan.hpp"
#include "pv/kernel/merkle.hpp"
#include "pv/runtime/transaction.hpp"

using namespace pv;

TEST_CASE("execution plan reports touched objects pointers and read write facts") {
    World world{"kernel"};
    REQUIRE(world.commit(world.object_delta("A", "Node"), Verifier{}).accepted);
    REQUIRE(world.commit(world.object_delta("B", "Node"), Verifier{}).accepted);

    Transaction tx;
    tx.label = "link A B";
    tx.delta = world.link_delta(world.object_by_name("A"), world.object_by_name("B"), "causes", 0.5, CausalRole::Structural);

    const auto prepared = prepare_transaction(world, tx, Verifier{});
    REQUIRE(prepared.committable);
    REQUIRE(prepared.execution_plan.resolved_ops.size() == 1);
    const auto& op = prepared.execution_plan.resolved_ops.front();
    REQUIRE(op.touched_objects.size() == 2);
    REQUIRE(op.touched_pointers.size() == 1);
    REQUIRE_FALSE(op.reads.empty());
    REQUIRE_FALSE(op.writes.empty());
    REQUIRE_FALSE(empty(prepared.execution_plan.plan_hash));
    REQUIRE(prepared.proof.has_value());
    REQUIRE(prepared.proof->before_root == compute_world_root(prepared.before).root);
    REQUIRE(prepared.proof->after_root == compute_world_root(*prepared.predicted_after).root);
}

TEST_CASE("law violations carry kernel object pointer and evidence references") {
    World world{"kernel"};
    REQUIRE(world.commit(world.object_delta("A", "Node"), Verifier{}).accepted);
    REQUIRE(world.commit(world.object_delta("B", "Node"), Verifier{}).accepted);

    Verifier verifier;
    verifier.add_builtin("bounded_weight");
    const auto rejected = world.commit(
        world.link_delta(world.object_by_name("A"), world.object_by_name("B"), "causes", 1.5, CausalRole::Structural),
        verifier);

    REQUIRE_FALSE(rejected.accepted);
    REQUIRE(rejected.violations.size() == 1);
    REQUIRE_FALSE(rejected.violations.front().objects.empty());
    REQUIRE_FALSE(rejected.violations.front().pointers.empty());
    REQUIRE_FALSE(rejected.violations.front().evidence.empty());
}
