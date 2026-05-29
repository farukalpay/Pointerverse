// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

#include "pv/normalize/canonical_event.hpp"

using namespace pv;

TEST_CASE("canonical event validates required generic fields") {
    CanonicalEvent event;
    event.id = "e1";
    event.source = "source";
    event.subject = "B";
    event.relation = "causes";

    REQUIRE_THROWS_AS(validate(event), std::invalid_argument);

    event.actor = "A";
    REQUIRE_NOTHROW(validate(event));
}

TEST_CASE("canonical event JSONL round trip is deterministic") {
    CanonicalEvent event;
    event.id = "e1";
    event.source = "source";
    event.kind = "observation";
    event.actor = "A";
    event.subject = "B";
    event.relation = "causes";
    event.observed_at_ms = 1710000000000LL;
    event.attributes.emplace("z", string_value("last"));
    event.attributes.emplace("a", uint64_value(7));

    const auto first = to_jsonl(event);
    const auto roundtrip = canonical_event_from_jsonl(first);
    REQUIRE(roundtrip.id == event.id);
    REQUIRE(roundtrip.attributes.at("a").kind == ValueKind::UInt64);
    REQUIRE(to_jsonl(roundtrip) == first);
}
