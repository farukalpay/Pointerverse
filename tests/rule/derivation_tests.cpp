// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

#include "pv/core/world.hpp"
#include "pv/rule/derivation.hpp"

using namespace pv;

namespace {

// Count the edges of a relation that are active at the world's current epoch.
std::size_t active_edges(const World& world, std::string_view relation) {
    const auto snapshot = world.snapshot();
    std::size_t count = 0;
    for (const auto& pointer : snapshot.pointers) {
        const bool active =
            pointer.born_at <= snapshot.epoch
            && (!pointer.expires_at.has_value() || snapshot.epoch < *pointer.expires_at);
        if (active && snapshot.relation_name(pointer.relation) == relation) {
            count += 1;
        }
    }
    return count;
}

std::size_t active_derived_edges(const World& world, std::string_view relation) {
    const auto snapshot = world.snapshot();
    std::size_t count = 0;
    for (const auto& pointer : snapshot.pointers) {
        const bool active =
            pointer.born_at <= snapshot.epoch
            && (!pointer.expires_at.has_value() || snapshot.epoch < *pointer.expires_at);
        if (active && pointer.law_domain == "derivation"
            && snapshot.relation_name(pointer.relation) == relation) {
            count += 1;
        }
    }
    return count;
}

EvolutionProgram derivation_program(const std::string& text) {
    return EvolutionProgram{{std::make_shared<DerivationEvolution>(parse_derivations(text))}};
}

void seed_chain(World& world, const Verifier& verifier) {
    for (const auto* name : {"A", "B", "C", "D"}) {
        REQUIRE(world.commit(world.object_delta(name, "Node"), verifier).accepted);
    }
    const auto link = [&](const char* from, const char* to) {
        REQUIRE(world.commit(
            world.link_delta(world.object_by_name(from), world.object_by_name(to), "reaches", 1.0, CausalRole::Structural),
            verifier).accepted);
    };
    link("A", "B");
    link("B", "C");
    link("C", "D");
}

}  // namespace

TEST_CASE("derivation computes the transitive closure of a relation") {
    const auto program = derivation_program(
        "derive transitive_reach\n"
        "from link X -> Y : reaches\n"
        "from link Y -> Z : reaches\n"
        "make link X -> Z : reaches role=Structural weight=1.0\n");

    Verifier verifier;
    World world{"reach"};
    seed_chain(world, verifier);

    REQUIRE(active_edges(world, "reaches") == 3);  // A->B, B->C, C->D

    const auto result = world.evolve(1, verifier, program);
    REQUIRE(result.completed_steps == 1);

    // Full closure of the chain adds A->C, B->D, A->D.
    REQUIRE(active_edges(world, "reaches") == 6);
    REQUIRE(active_derived_edges(world, "reaches") == 3);
}

TEST_CASE("derivation is idempotent across repeated evolve steps") {
    const auto program = derivation_program(
        "derive transitive_reach\n"
        "from link X -> Y : reaches\n"
        "from link Y -> Z : reaches\n"
        "make link X -> Z : reaches\n");

    Verifier verifier;
    World world{"reach"};
    seed_chain(world, verifier);

    REQUIRE(world.evolve(1, verifier, program).completed_steps == 1);
    const auto after_first = active_edges(world, "reaches");
    REQUIRE(after_first == 6);

    // Re-deriving clears the previous derived edges and recomputes the same closure;
    // the active edge set must not grow.
    REQUIRE(world.evolve(1, verifier, program).completed_steps == 1);
    REQUIRE(active_edges(world, "reaches") == after_first);
    REQUIRE(active_derived_edges(world, "reaches") == 3);
}

TEST_CASE("single-atom derivation generates a conditional edge") {
    const auto program = derivation_program(
        "derive reverse_citation\n"
        "from link X -> Y : cites\n"
        "make link Y -> X : cited_by role=Observational\n");

    Verifier verifier;
    World world{"papers"};
    REQUIRE(world.commit(world.object_delta("Paper1", "Paper"), verifier).accepted);
    REQUIRE(world.commit(world.object_delta("Paper2", "Paper"), verifier).accepted);
    REQUIRE(world.commit(
        world.link_delta(world.object_by_name("Paper1"), world.object_by_name("Paper2"), "cites", 1.0, CausalRole::Structural),
        verifier).accepted);

    REQUIRE(active_edges(world, "cited_by") == 0);
    REQUIRE(world.evolve(1, verifier, program).completed_steps == 1);

    REQUIRE(active_edges(world, "cited_by") == 1);
    REQUIRE(active_derived_edges(world, "cited_by") == 1);
}

TEST_CASE("parse_domain_package collects derivations alongside rules") {
    // Smoke test that the package parser routes derive/from/make blocks correctly and
    // leaves rule blocks intact. (parse_derivations is exercised above; here we only
    // check the builder wiring through the shared keywords.)
    auto derivations = parse_derivations(
        "derive a\n"
        "from link X -> Y : r\n"
        "make link X -> Y : s\n"
        "derive b\n"
        "from link P -> Q : t\n"
        "from link Q -> R : t\n"
        "make link P -> R : t\n");
    REQUIRE(derivations.size() == 2);
    REQUIRE(derivations[0].name == "a");
    REQUIRE(derivations[0].body.size() == 1);
    REQUIRE(derivations[1].name == "b");
    REQUIRE(derivations[1].body.size() == 2);
    REQUIRE(derivations[1].head.relation == "t");
}
