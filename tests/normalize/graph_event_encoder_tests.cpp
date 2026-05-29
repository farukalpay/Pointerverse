// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "pv/normalize/graph_event_encoder.hpp"

using namespace pv;

TEST_CASE("graph event encoder maps canonical events to typed graph events") {
    CanonicalEvent event;
    event.id = "e1";
    event.source = "source";
    event.kind = "observation";
    event.actor = "A";
    event.subject = "B";
    event.relation = "causes";
    event.observed_at_ms = 42;
    event.attributes.emplace("actor_type", string_value("Agent"));
    event.attributes.emplace("subject_type", string_value("File"));
    event.attributes.emplace("weight", float64_value(0.5));
    event.attributes.emplace("role", string_value("Generative"));
    event.attributes.emplace("confidence", uint64_value(99));

    const auto graph = GraphEventEncoder{}.encode(event);

    REQUIRE(graph.id == "e1");
    REQUIRE(graph.source == "source");
    REQUIRE(graph.from == "A");
    REQUIRE(graph.from_type == "Agent");
    REQUIRE(graph.to == "B");
    REQUIRE(graph.to_type == "File");
    REQUIRE(graph.relation == "causes");
    REQUIRE(graph.weight == 0.5);
    REQUIRE(graph.role == "Generative");
    REQUIRE(graph.attributes.contains("confidence"));
    REQUIRE_FALSE(graph.attributes.contains("actor_type"));
    REQUIRE(graph.attributes.at("observed_at_ms").kind == ValueKind::Int64);
}

TEST_CASE("graph event JSONL round trip is deterministic") {
    GraphEvent left;
    left.id = "e1";
    left.source = "source";
    left.from = "A";
    left.from_type = "Entity";
    left.to = "B";
    left.to_type = "Entity";
    left.relation = "causes";
    left.attributes.emplace("z", string_value("last"));
    left.attributes.emplace("a", uint64_value(7));

    GraphEvent right = left;
    right.attributes.clear();
    right.attributes.emplace("a", uint64_value(7));
    right.attributes.emplace("z", string_value("last"));

    REQUIRE(to_jsonl(left) == to_jsonl(right));
    REQUIRE(graph_event_from_jsonl(to_jsonl(left)).attributes.at("a").kind == ValueKind::UInt64);
}
