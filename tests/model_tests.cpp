#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "pointerverse/model.hpp"

using pointerverse::ExternalEvent;
using pointerverse::compute_pressure;
using pointerverse::compress_external_event;
using pointerverse::external_event_from_json;

TEST_CASE("pressure formula is deterministic and bounded") {
    const auto pressure = compute_pressure(1.0, 0.5, 0.5, 0.25);

    REQUIRE(pressure.magnitude == Catch::Approx(0.70));
    REQUIRE(pressure.magnitude >= 0.0);
    REQUIRE(pressure.magnitude <= 1.0);
}

TEST_CASE("external conflict compresses into unresolved constraint") {
    ExternalEvent event;
    event.id = "E0";
    event.kind = "conflict";
    event.weight = 1.0;
    event.metrics = {
        {"law_residual", 1.0},
        {"graph_entropy", 0.4},
        {"state_drift", 0.7},
        {"noncommutativity", 0.2}
    };

    const auto compression = compress_external_event(event);
    REQUIRE(compression.object_type == "UnresolvedConstraint");
    REQUIRE(compression.relation == "contradicts");
    REQUIRE(compression.creates_constraint);
    REQUIRE(compression.pressure.magnitude > 0.7);
}

TEST_CASE("external event JSON parser accepts structured event payloads") {
    const auto event = external_event_from_json({
        {"id", "E1"},
        {"kind", "missing_relation"},
        {"weight", 0.8},
        {"metrics", {{"law_residual", 0.4}}},
        {"tags", {"gap"}},
        {"links", {{{"from", "E1"}, {"to", "E2"}, {"relation", "requires_relation"}, {"weight", 0.7}}}}
    });

    REQUIRE(event.id == "E1");
    REQUIRE(event.kind == "missing_relation");
    REQUIRE(event.tags.size() == 1);
    REQUIRE(event.links.size() == 1);
    REQUIRE(event.links.front().relation == "requires_relation");
}
