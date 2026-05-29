// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "pv/core/world.hpp"
#include "pv/trace/replayer.hpp"

using namespace pv;

TEST_CASE("trace replay reconstructs exported world history") {
    Verifier verifier;
    verifier.add_builtin("reject_dangling_pointer");
    verifier.add_builtin("bounded_weight");

    World world{"seed"};
    REQUIRE(world.commit(world.object_delta("A", "Node"), verifier).accepted);
    REQUIRE(world.commit(world.object_delta("B", "Node"), verifier).accepted);
    REQUIRE(world.commit(world.link_delta(
        world.object_by_name("A"),
        world.object_by_name("B"),
        "causes",
        0.7,
        CausalRole::Structural), verifier).accepted);
    REQUIRE(world.evolve(2, verifier).rejected_steps == 0);

    const auto replay = TraceReplayer{}.replay_jsonl(world.trace().to_jsonl(), verifier);

    REQUIRE(replay.errors.empty());
    REQUIRE(replay.events_read == world.trace().size());
    REQUIRE(replay.events_replayed >= 5);
    REQUIRE(replay.metadata_events > 0);
    REQUIRE(replay.final_hash == world.hash());
    REQUIRE(replay.world.snapshot().structural_hash() == world.snapshot().structural_hash());
}

TEST_CASE("trace replay treats law and rejected transition events as metadata") {
    const auto jsonl =
        "{\"epoch\":0,\"event\":\"world.transition.rejected\",\"fields\":{\"world\":\"seed\",\"reason\":\"no\"},\"measurements\":{\"violations\":1}}\n"
        "{\"epoch\":0,\"event\":\"law.check\",\"fields\":{\"world\":\"seed\",\"law\":\"bounded_weight\",\"status\":\"error\",\"detail\":\"no\"},\"measurements\":{\"magnitude\":1}}\n";

    const auto replay = TraceReplayer{}.replay_jsonl(jsonl, Verifier{});

    REQUIRE(replay.errors.empty());
    REQUIRE(replay.events_read == 2);
    REQUIRE(replay.events_replayed == 0);
    REQUIRE(replay.metadata_events == 2);
    REQUIRE(replay.world.epoch().value == 0);
}

TEST_CASE("trace replay reports malformed and unsupported events") {
    const auto jsonl =
        "not json\n"
        "{\"epoch\":1,\"event\":\"realm.spawn\",\"fields\":{},\"measurements\":{}}\n";

    const auto replay = TraceReplayer{}.replay_jsonl(jsonl, Verifier{});

    REQUIRE(replay.errors.size() == 2);
    REQUIRE(replay.events_read == 2);
    REQUIRE(replay.events_replayed == 0);
}
