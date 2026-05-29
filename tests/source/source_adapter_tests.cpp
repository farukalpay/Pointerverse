// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <sstream>

#include "pv/core/value.hpp"
#include "pv/source/source_adapter.hpp"

using namespace pv;

TEST_CASE("JSONL source adapter reads canonical-like events and typed attributes") {
    std::istringstream input{
        "{\"id\":\"1\",\"source\":\"sensor\",\"actor\":\"A\",\"subject\":\"B\",\"relation\":\"causes\",\"ts\":1710000000,\"weight\":0.75,\"active\":true,\"meta\":{\"nested\":true}}\n"
        "{\"event_id\":\"2\",\"agent\":\"Agent0\",\"target\":\"FileA\",\"action\":\"modifies\",\"ts_ms\":1710000001000,\"count\":2}\n"
        "{\"id\":\"broken\"\n"
    };

    const auto batch = JsonlSourceAdapter{"jsonl"}.read(input);

    REQUIRE(batch.events.size() == 2);
    REQUIRE(batch.errors.size() == 2);
    REQUIRE(batch.events[0].id == "1");
    REQUIRE(batch.events[0].source == "sensor");
    REQUIRE(batch.events[0].actor == "A");
    REQUIRE(batch.events[0].subject == "B");
    REQUIRE(batch.events[0].relation == "causes");
    REQUIRE(batch.events[0].observed_at_ms == 1710000000000LL);
    REQUIRE(batch.events[0].attributes.at("weight").kind == ValueKind::Float64);
    REQUIRE(batch.events[0].attributes.at("active").kind == ValueKind::Bool);
    REQUIRE(batch.events[1].source == "jsonl");
    REQUIRE(batch.events[1].actor == "Agent0");
    REQUIRE(batch.events[1].subject == "FileA");
    REQUIRE(batch.events[1].relation == "modifies");
    REQUIRE(batch.events[1].attributes.at("count").kind == ValueKind::UInt64);
}

TEST_CASE("source event keys reject tabs and newlines") {
    REQUIRE(valid_source_event_key("source-1"));
    REQUIRE_FALSE(valid_source_event_key(""));
    REQUIRE_FALSE(valid_source_event_key("source\t1"));
    REQUIRE_FALSE(valid_source_event_key("source\n1"));
}
