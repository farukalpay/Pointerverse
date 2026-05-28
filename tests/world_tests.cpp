#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "pointerverse/observer.hpp"
#include "pointerverse/world.hpp"

using pointerverse::CausalTag;
using pointerverse::ObjectHandle;
using pointerverse::Observer;
using pointerverse::Scalar;
using pointerverse::StateVector;
using pointerverse::World;

TEST_CASE("world creates stable object handles and semantic relations") {
    World world;
    const auto a = world.create_object("StateNode", "A", {{"dim", "2"}});
    const auto b = world.create_object("StateNode", "B", {{"dim", "2"}});

    REQUIRE(world.contains(a));
    REQUIRE(world.contains(b));
    REQUIRE(world.object_by_name("A") == a);

    const auto relation = world.link(a, b, "correlates_with", 0.8, CausalTag::Causal);
    REQUIRE(world.relations().size() == 1);
    REQUIRE(world.object(a).outgoing.size() == 1);
    REQUIRE(world.object(b).incoming.size() == 1);
    REQUIRE(world.relation(relation).relation == "correlates_with");
}

TEST_CASE("world rejects invalid relation handles") {
    World world;
    const auto a = world.create_object("StateNode", "A");
    const ObjectHandle invalid{999, 1};

    REQUIRE_THROWS(world.link(a, invalid, "causes"));
}

TEST_CASE("morphism composition checks type compatibility") {
    World world;
    const auto rotate = world.register_morphism("Stabilize", "StateNode", "StateNode", "stabilize");
    const auto measure = world.register_morphism("Readout", "StateNode", "Observation", "measure");
    const auto forget = world.register_morphism("Forget", "RichObject", "SimpleObject", "forget");
    REQUIRE(rotate.value != 0);
    REQUIRE(measure.value != 0);
    REQUIRE(forget.value != 0);

    const auto valid = world.compose("Readout", "Stabilize");
    REQUIRE(valid.valid);
    REQUIRE(valid.from_type == "StateNode");
    REQUIRE(valid.to_type == "Observation");

    const auto invalid = world.compose("Stabilize", "Forget");
    REQUIRE_FALSE(invalid.valid);
    REQUIRE_FALSE(invalid.errors.empty());
}

TEST_CASE("evolve records snapshot trace and law results") {
    World world;
    const auto a = world.create_object("StateNode", "A", {{"dim", "2"}});
    world.set_state(a, StateVector{{Scalar{1.0, 0.0}, Scalar{1.0, 0.0}}});
    world.add_builtin_law("normalization", 1e-9);
    world.add_builtin_law("probability_mass", 1e-9);

    const auto result = world.evolve(3);
    REQUIRE(result.completed_steps == 3);
    REQUIRE(result.passed);
    REQUIRE(world.trace().size() == 3);
    REQUIRE(world.snapshot().max_normalization_error == Catch::Approx(0.0).margin(1e-12));
}

TEST_CASE("contradiction seeds create pressure and regions after evolution") {
    World world;
    const auto result = world.seed_contradiction(6);
    REQUIRE(result.objects.size() == 6);
    REQUIRE(result.pointers.size() == 6);
    REQUIRE(world.snapshot().max_pressure > 0.75);

    const auto evolve = world.evolve(2);
    REQUIRE(evolve.completed_steps == 2);
    REQUIRE_FALSE(world.regions().empty());
    REQUIRE(world.regions().front().pressure.magnitude > 0.75);
}

TEST_CASE("failed morphism application records pressure without graph mutation") {
    World world;
    const auto a = world.create_object("StateNode", "A", {{"dim", "2"}});
    const auto id = world.register_morphism("Readout", "Observation", "Observation", "measure");
    const auto before_objects = world.snapshot().object_count;
    const auto before_relations = world.snapshot().relation_count;

    const auto result = world.apply_morphism(id, a);
    REQUIRE_FALSE(result.valid);
    REQUIRE_FALSE(result.applied);
    REQUIRE(world.snapshot().object_count == before_objects);
    REQUIRE(world.snapshot().relation_count == before_relations);
    REQUIRE(world.pressure(a).magnitude > 0.0);
    REQUIRE_FALSE(world.trace().steps().back().structured_events.empty());
}

TEST_CASE("scoped observers deny unreachable measurements") {
    World world;
    const auto observer_handle = world.create_object("Observer", "O1", {{"scope", "1"}});
    const auto a = world.create_object("StateNode", "A", {{"dim", "2"}});
    const auto b = world.create_object("StateNode", "B", {{"dim", "2"}});
    const auto observes = world.link(observer_handle, a, "observes", 1.0, CausalTag::Observational);
    REQUIRE(observes.value != 0);

    const Observer observer{"O1", observer_handle, 1};
    const auto allowed = observer.observe(world, a, "pressure");
    REQUIRE_FALSE(allowed.denied);

    const auto denied = observer.observe(world, b, "pressure");
    REQUIRE(denied.denied);
    REQUIRE(denied.denial_reason == "observer scope too narrow");
}
