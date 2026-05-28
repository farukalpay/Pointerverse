// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <sstream>

#include "pv/ingest/jsonl_adapter.hpp"

using namespace pv;

TEST_CASE("JSONL evidence adapter reads agent log events") {
    std::istringstream input{
        "{\"id\":\"1\",\"agent\":\"Agent0\",\"event\":\"read_file\",\"path\":\"src/main.cpp\",\"ts\":1710000000}\n"
        "{\"id\":\"2\",\"agent\":\"Agent0\",\"event\":\"write_file\",\"path\":\"src/main.cpp\",\"ts_ms\":1710000001000}\n"
        "{\"id\":\"3\",\"agent\":\"Agent0\",\"event\":\"create_pr\",\"pr\":\"PR42\",\"ts\":1710000002}\n"
    };

    const auto batch = JsonlEvidenceAdapter{"agent-log"}.read(input);

    REQUIRE(batch.errors.empty());
    REQUIRE(batch.events.size() == 3);
    REQUIRE(batch.events[0].source == "agent-log");
    REQUIRE(batch.events[0].event_id == "1");
    REQUIRE(batch.events[0].actor == "Agent0");
    REQUIRE(batch.events[0].action == "read_file");
    REQUIRE(batch.events[0].target == "src/main.cpp");
    REQUIRE(batch.events[0].timestamp_ms == 1710000000000ULL);
    REQUIRE(batch.events[1].timestamp_ms == 1710000001000ULL);
}

TEST_CASE("JSONL evidence adapter reports line-specific parse errors") {
    std::istringstream input{
        "{\"id\":\"1\",\"event\":\"read_file\"}\n"
        "{\"id\":\"2\"\n"
        "{\"event\":\"write_file\"}\n"
    };

    const auto batch = JsonlEvidenceAdapter{"agent-log"}.read(input);

    REQUIRE(batch.events.size() == 1);
    REQUIRE(batch.errors.size() == 2);
    REQUIRE(batch.errors[0].line == 2);
    REQUIRE(batch.errors[1].line == 3);
}
