#include <catch2/catch_test_macros.hpp>

#include "pointerverse/analyzer.hpp"
#include "pointerverse/world.hpp"

using pointerverse::Analyzer;
using pointerverse::StateVector;
using pointerverse::World;

TEST_CASE("analyzer reports deterministic invariants after stable evolution") {
    World world;
    const auto object = world.create_object("StateNode", "A", {{"dim", "2"}});
    REQUIRE(world.contains(object));
    world.add_builtin_law("normalization");
    const auto evolve = world.evolve(2);
    REQUIRE(evolve.passed);

    const Analyzer analyzer;
    const auto report = analyzer.scan(world.trace());

    REQUIRE(report.stable());
    REQUIRE_FALSE(report.invariants.empty());
    REQUIRE(report.anomalies.empty());
}

TEST_CASE("analyzer flags empty traces") {
    World world;

    const Analyzer analyzer;
    const auto report = analyzer.scan(world.trace());

    REQUIRE_FALSE(report.stable());
    REQUIRE_FALSE(report.anomalies.empty());
}
