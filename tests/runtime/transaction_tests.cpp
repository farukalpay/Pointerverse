// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "pv/runtime/transaction.hpp"

using namespace pv;

TEST_CASE("transaction prepare rejects wrong epoch hash and empty delta by default") {
    World world{"seed"};
    Verifier verifier;

    Transaction empty;
    auto prepared_empty = prepare_transaction(world, empty, verifier);
    REQUIRE_FALSE(prepared_empty.committable);

    Transaction wrong_epoch;
    wrong_epoch.allow_empty = true;
    wrong_epoch.expected_base_epoch = Epoch{99};
    auto prepared_epoch = prepare_transaction(world, wrong_epoch, verifier);
    REQUIRE_FALSE(prepared_epoch.committable);

    Transaction wrong_hash;
    wrong_hash.allow_empty = true;
    wrong_hash.input_snapshot_hash = canonical_hash(Delta{});
    auto prepared_hash = prepare_transaction(world, wrong_hash, verifier);
    REQUIRE_FALSE(prepared_hash.committable);
}

TEST_CASE("prepared accepted transaction preserves before and after hash") {
    World world{"seed"};
    Verifier verifier;

    Transaction tx;
    tx.label = "create A";
    tx.delta = world.object_delta("A", "Node");

    const auto prepared = prepare_transaction(world, tx, verifier);
    REQUIRE(prepared.committable);
    REQUIRE(prepared.predicted_after.has_value());

    const auto before_hash = prepared.before.canonical_hash();
    const auto after_hash = prepared.predicted_after->canonical_hash();
    const auto result = commit_prepared(world, prepared);

    REQUIRE(result.accepted);
    REQUIRE(before_hash != after_hash);
    REQUIRE(world.canonical_hash() == after_hash);
    REQUIRE(result.before_epoch == Epoch{0});
    REQUIRE(result.after_epoch == Epoch{1});
}
