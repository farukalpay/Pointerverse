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

TEST_CASE("pattern rule supports before requirements") {
    auto rules = parse_rules(
        "rule no_write_without_read\n"
        "when link Agent -> File : modifies\n"
        "require before link Agent -> File : reads\n"
        "deny reason \"{from} modifies {to} without prior read relation\"\n");

    Verifier verifier;
    verifier.add(std::make_shared<PatternLaw>(rules.front()));

    World world{"audit"};
    REQUIRE(world.commit(world.object_delta("Agent0", "Agent"), verifier).accepted);
    REQUIRE(world.commit(world.object_delta("FileA", "File"), verifier).accepted);

    Delta same_transaction;
    same_transaction.append_link(PointerCreate{
        ObjectRef{world.object_by_name("Agent0")},
        ObjectRef{world.object_by_name("FileA")},
        world.relation_type("reads"),
        CausalRole::Structural,
        Weight{1.0},
        "core",
        {}
    });
    same_transaction.append_link(PointerCreate{
        ObjectRef{world.object_by_name("Agent0")},
        ObjectRef{world.object_by_name("FileA")},
        world.relation_type("modifies"),
        CausalRole::Structural,
        Weight{1.0},
        "core",
        {}
    });
    REQUIRE_FALSE(world.commit(same_transaction, verifier).accepted);

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

TEST_CASE("forbid rule rejects a trigger when the forbidden relation is present") {
    auto rules = parse_rules(
        "rule never_modify_quarantined\n"
        "when link Agent -> File : modifies\n"
        "forbid after link Agent -> File : quarantined\n"
        "deny reason \"{from} modifies quarantined {to}\"\n");
    REQUIRE(rules.size() == 1);
    REQUIRE(rules.front().requirements.size() == 1);
    REQUIRE(rules.front().requirements.front().forbidden);

    Verifier verifier;
    verifier.add(std::make_shared<PatternLaw>(rules.front()));

    World world{"audit"};
    REQUIRE(world.commit(world.object_delta("Agent0", "Agent"), verifier).accepted);
    REQUIRE(world.commit(world.object_delta("FileA", "File"), verifier).accepted);
    REQUIRE(world.commit(
        world.link_delta(
            world.object_by_name("Agent0"),
            world.object_by_name("FileA"),
            "quarantined",
            1.0,
            CausalRole::Structural),
        verifier).accepted);

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
    REQUIRE(rejected.violations.front().law == "never_modify_quarantined");
    REQUIRE(rejected.violations.front().explanation == "Agent0 modifies quarantined FileA");
}

TEST_CASE("forbid rule accepts a trigger when the forbidden relation is absent") {
    auto rules = parse_rules(
        "rule never_modify_quarantined\n"
        "when link Agent -> File : modifies\n"
        "forbid after link Agent -> File : quarantined\n"
        "deny reason \"{from} modifies quarantined {to}\"\n");

    Verifier verifier;
    verifier.add(std::make_shared<PatternLaw>(rules.front()));

    World world{"audit"};
    REQUIRE(world.commit(world.object_delta("Agent0", "Agent"), verifier).accepted);
    REQUIRE(world.commit(world.object_delta("FileA", "File"), verifier).accepted);
    REQUIRE(world.commit(
        world.link_delta(
            world.object_by_name("Agent0"),
            world.object_by_name("FileA"),
            "modifies",
            1.0,
            CausalRole::Structural),
        verifier).accepted);
}

TEST_CASE("forbid with ~ endpoint binding matches a third object, not just the trigger endpoint") {
    // '~File' unpins the requirement target from the trigger's File: modifying any file
    // is forbidden while the agent already holds some *other* quarantined file. The old,
    // pinned form could never express this -- it only saw the trigger's own endpoints.
    auto rules = parse_rules(
        "rule no_modify_while_any_quarantined\n"
        "when link Agent -> File : modifies\n"
        "forbid exists link Agent -> ~File : quarantined\n"
        "deny reason \"{from} modifies {to} while holding a quarantined file\"\n");
    REQUIRE(rules.size() == 1);
    REQUIRE(rules.front().requirements.size() == 1);
    REQUIRE(rules.front().requirements.front().forbidden);
    REQUIRE(rules.front().requirements.front().from_binding == PatternEndpointBinding::TriggerFrom);
    REQUIRE(rules.front().requirements.front().to_binding == PatternEndpointBinding::Any);

    Verifier verifier;
    verifier.add(std::make_shared<PatternLaw>(rules.front()));

    World world{"audit"};
    REQUIRE(world.commit(world.object_delta("Agent0", "Agent"), verifier).accepted);
    REQUIRE(world.commit(world.object_delta("FileA", "File"), verifier).accepted);
    REQUIRE(world.commit(world.object_delta("FileB", "File"), verifier).accepted);
    // Quarantine a DIFFERENT file than the one about to be modified.
    REQUIRE(world.commit(
        world.link_delta(
            world.object_by_name("Agent0"),
            world.object_by_name("FileB"),
            "quarantined",
            1.0,
            CausalRole::Structural),
        verifier).accepted);

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
    REQUIRE(rejected.violations.front().law == "no_modify_while_any_quarantined");
}

TEST_CASE("pinned endpoint (no ~) stays bound to the trigger and ignores a third object") {
    // Same scenario, but the requirement is pinned to the trigger's File. Quarantining a
    // different file must NOT trip the rule -- this is the original, back-compatible behavior.
    auto rules = parse_rules(
        "rule no_modify_while_this_quarantined\n"
        "when link Agent -> File : modifies\n"
        "forbid exists link Agent -> File : quarantined\n"
        "deny reason \"{from} modifies quarantined {to}\"\n");
    REQUIRE(rules.front().requirements.front().from_binding == PatternEndpointBinding::TriggerFrom);
    REQUIRE(rules.front().requirements.front().to_binding == PatternEndpointBinding::TriggerTo);

    Verifier verifier;
    verifier.add(std::make_shared<PatternLaw>(rules.front()));

    World world{"audit"};
    REQUIRE(world.commit(world.object_delta("Agent0", "Agent"), verifier).accepted);
    REQUIRE(world.commit(world.object_delta("FileA", "File"), verifier).accepted);
    REQUIRE(world.commit(world.object_delta("FileB", "File"), verifier).accepted);
    REQUIRE(world.commit(
        world.link_delta(
            world.object_by_name("Agent0"),
            world.object_by_name("FileB"),
            "quarantined",
            1.0,
            CausalRole::Structural),
        verifier).accepted);

    // Modifying FileA is fine: only FileB is quarantined and the requirement is pinned to FileA.
    REQUIRE(world.commit(
        world.link_delta(
            world.object_by_name("Agent0"),
            world.object_by_name("FileA"),
            "modifies",
            1.0,
            CausalRole::Structural),
        verifier).accepted);
}

TEST_CASE("observe verifier records violations without rejecting commits") {
    auto rules = parse_rules(
        "rule no_write_without_read\n"
        "when link Agent -> File : modifies\n"
        "require exists link Agent -> File : reads\n"
        "deny reason \"{from} modifies {to} without prior read relation\"\n");

    Verifier verifier{VerificationMode::Observe};
    verifier.add(std::make_shared<PatternLaw>(rules.front()));

    World world{"audit"};
    REQUIRE(world.commit(world.object_delta("Agent0", "Agent"), verifier).accepted);
    REQUIRE(world.commit(world.object_delta("FileA", "File"), verifier).accepted);

    const auto observed = world.commit(
        world.link_delta(
            world.object_by_name("Agent0"),
            world.object_by_name("FileA"),
            "modifies",
            1.0,
            CausalRole::Structural),
        verifier);

    REQUIRE(observed.accepted);
    REQUIRE(observed.violations.size() == 1);
    REQUIRE(observed.law_statuses.front().severity == Severity::Error);
}
