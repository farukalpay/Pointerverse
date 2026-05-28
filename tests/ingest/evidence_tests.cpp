// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "pv/ingest/evidence.hpp"
#include "pv/ingest/event_schema.hpp"

using namespace pv;

TEST_CASE("evidence helpers expose attributes and stable evidence object names") {
    EvidenceEvent event;
    event.source = "agent-log";
    event.event_id = "42";
    event.attributes.push_back({"path", "src/main.cpp"});

    REQUIRE(attribute_value(event, "path") == "src/main.cpp");
    REQUIRE_FALSE(attribute_value(event, "missing").has_value());
    REQUIRE(evidence_object_name(event) == "Evidence/agent-log/42");
    REQUIRE(valid_evidence_key("agent-log"));
    REQUIRE_FALSE(valid_evidence_key("bad\tkey"));
}

TEST_CASE("timestamp normalization treats ts as seconds below millisecond threshold") {
    REQUIRE(normalize_timestamp_ms(1710000000ULL) == 1710000000000ULL);
    REQUIRE(normalize_timestamp_ms(1710000000001ULL) == 1710000000001ULL);
}
