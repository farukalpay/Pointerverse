// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "pv/core/world.hpp"
#include "pv/rule/rule_engine.hpp"

using namespace pv;

TEST_CASE("pattern rule rejects a trigger without its required relation") {
    auto rules = parse_rules(
        "rule no_write_without_read\n"
        "when link Agent -> File : modifies\n"
        "require exists link Agent -> File : reads\n"
        "deny reason \"{from} modifies {to} without prior read relation\"\n");
    REQUIRE(rules.size() == 1);

    RuleEngine engine;
    engine.add(rules.front());

    Verifier verifier;
    verifier.add(engine.make_law("no_write_without_read"));

    World world{"audit"};
    REQUIRE(world.commit(world.object_delta("Agent0", "Agent"), verifier).accepted);
    REQUIRE(world.commit(world.object_delta("FileA", "File"), verifier).accepted);

    const auto rejected = world.commit(
        world.link_delta(
            world.object_by_name("Agent0"),
            world.object_by_name("FileA"),
            "modifies",
            1.0,
            CausalRole::Structural),
        verifier);

    REQUIRE_FALSE(rejected.accepted);
    REQUIRE(rejected.violations.size() == 1);
    REQUIRE(rejected.violations.front().law == "no_write_without_read");
    REQUIRE(rejected.violations.front().explanation == "Agent0 modifies FileA without prior read relation");
}

TEST_CASE("pattern rule accepts a trigger when the required relation exists") {
    auto rules = parse_rules(
        "rule no_write_without_read\n"
        "when link Agent -> File : modifies\n"
        "require exists link Agent -> File : reads\n"
        "deny reason \"{from} modifies {to} without prior read relation\"\n");

    Verifier verifier;
    verifier.add(std::make_shared<PatternLaw>(rules.front()));

    World world{"audit"};
    REQUIRE(world.commit(world.object_delta("Agent0", "Agent"), verifier).accepted);
    REQUIRE(world.commit(world.object_delta("FileA", "File"), verifier).accepted);
    REQUIRE(world.commit(
        world.link_delta(
            world.object_by_name("Agent0"),
            world.object_by_name("FileA"),
            "reads",
            1.0,
            CausalRole::Structural),
        verifier).accepted);

    REQUIRE(world.commit(
        world.link_delta(
            world.object_by_name("Agent0"),
            world.object_by_name("FileA"),
            "modifies",
            1.0,
            CausalRole::Structural),
        verifier).accepted);
}
