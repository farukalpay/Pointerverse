// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

#include "pv/category/composition.hpp"
#include "pv/core/world.hpp"
#include "pv/observer/observer.hpp"

using namespace pv;

namespace {

std::vector<std::string> normalized_morphism_path(const std::vector<TraceEvent>& events) {
    std::vector<std::string> out;
    for (const auto& event : events) {
        if (event.event != "morphism.apply") {
            continue;
        }
        const auto iter = event.fields.find("name");
        if (iter != event.fields.end() && iter->second != "id") {
            out.push_back(iter->second);
        }
    }
    return out;
}

std::vector<std::string> law_status_class(const CommitResult& result) {
    std::vector<std::string> out;
    for (const auto& status : result.law_statuses) {
        out.push_back(status.law + ":" + (status.passed ? "ok" : to_string(status.severity)));
    }
    return out;
}

void seed_types(World& world) {
    (void)world.type_id("Node");
    (void)world.type_id("Observation");
    (void)world.type_id("Region");
    (void)world.type_id("Archive");
}

}  // namespace

TEST_CASE("composition checks domain and codomain") {
    World world{"seed"};
    const auto node = world.type_id("Node");
    const auto observation = world.type_id("Observation");
    const auto region = world.type_id("Region");

    auto stabilize = std::make_shared<DefinedMorphism>("Stabilize", MorphismSignature{node, node});
    auto readout = std::make_shared<DefinedMorphism>("Readout", MorphismSignature{node, observation});
    auto compress = std::make_shared<DefinedMorphism>("Compress", MorphismSignature{region, node});

    const auto valid = compose(readout, stabilize);
    REQUIRE(valid.has_value());
    REQUIRE((*valid)->signature().domain == node);
    REQUIRE((*valid)->signature().codomain == observation);

    const auto invalid = compose(compress, stabilize);
    REQUIRE_FALSE(invalid.has_value());
    REQUIRE(invalid.error() == CompositionError::DomainCodomainMismatch);
}

TEST_CASE("identity morphism is left identity under world equivalence") {
    Verifier verifier;
    verifier.add_builtin("preserve_existing_identity");

    World left{"seed"};
    World right{"seed"};
    seed_types(left);
    seed_types(right);
    REQUIRE(left.commit(left.object_delta("A", "Node"), verifier).accepted);
    REQUIRE(right.commit(right.object_delta("A", "Node"), verifier).accepted);

    const auto node = left.type_id("Node");
    const auto observation = left.type_id("Observation");
    auto f = std::make_shared<DefinedMorphism>("Readout", MorphismSignature{node, observation});
    auto id = std::make_shared<IdentityMorphism>(observation);

    const auto composed = compose(id, f);
    REQUIRE(composed.has_value());

    const auto direct = left.commit(f->apply(left.snapshot(), Selection{{left.object_by_name("A")}, {}}), verifier);
    const auto via_identity = right.commit((*composed)->apply(right.snapshot(), Selection{{right.object_by_name("A")}, {}}), verifier);

    const Observer observer{"test"};
    REQUIRE(direct.accepted == via_identity.accepted);
    REQUIRE(left.hash() == right.hash());
    REQUIRE(left.snapshot().structural_hash() == right.snapshot().structural_hash());
    REQUIRE(observer.inspect_graph(left.snapshot()).body == observer.inspect_graph(right.snapshot()).body);
    REQUIRE(law_status_class(direct) == law_status_class(via_identity));
    REQUIRE(normalized_morphism_path(direct.events) == normalized_morphism_path(via_identity.events));
}

TEST_CASE("identity morphism is right identity under world equivalence") {
    Verifier verifier;
    verifier.add_builtin("preserve_existing_identity");

    World left{"seed"};
    World right{"seed"};
    seed_types(left);
    seed_types(right);
    REQUIRE(left.commit(left.object_delta("A", "Node"), verifier).accepted);
    REQUIRE(right.commit(right.object_delta("A", "Node"), verifier).accepted);

    const auto node = left.type_id("Node");
    const auto observation = left.type_id("Observation");
    auto f = std::make_shared<DefinedMorphism>("Readout", MorphismSignature{node, observation});
    auto id = std::make_shared<IdentityMorphism>(node);

    const auto composed = compose(f, id);
    REQUIRE(composed.has_value());

    const auto direct = left.commit(f->apply(left.snapshot(), Selection{{left.object_by_name("A")}, {}}), verifier);
    const auto via_identity = right.commit((*composed)->apply(right.snapshot(), Selection{{right.object_by_name("A")}, {}}), verifier);

    const Observer observer{"test"};
    REQUIRE(direct.accepted == via_identity.accepted);
    REQUIRE(left.hash() == right.hash());
    REQUIRE(left.snapshot().structural_hash() == right.snapshot().structural_hash());
    REQUIRE(observer.inspect_graph(left.snapshot()).body == observer.inspect_graph(right.snapshot()).body);
    REQUIRE(law_status_class(direct) == law_status_class(via_identity));
    REQUIRE(normalized_morphism_path(direct.events) == normalized_morphism_path(via_identity.events));
}

TEST_CASE("composition applies first morphism then second morphism") {
    Verifier verifier;
    verifier.add_builtin("preserve_existing_identity");

    World world{"seed"};
    seed_types(world);
    REQUIRE(world.commit(world.object_delta("A", "Node"), verifier).accepted);

    const auto node = world.type_id("Node");
    const auto observation = world.type_id("Observation");
    const auto region = world.type_id("Region");
    auto f = std::make_shared<DefinedMorphism>("f", MorphismSignature{node, observation});
    auto g = std::make_shared<DefinedMorphism>("g", MorphismSignature{observation, region});

    const auto gf = compose(g, f);
    REQUIRE(gf.has_value());

    const auto result = world.commit((*gf)->apply(world.snapshot(), Selection{{world.object_by_name("A")}, {}}), verifier);
    REQUIRE(result.accepted);
    REQUIRE(world.object(world.object_by_name("A")).type == region);
    REQUIRE(normalized_morphism_path(result.events) == std::vector<std::string>{"f", "g"});
}

TEST_CASE("associative paths agree by hash, law status, and observer projection") {
    Verifier verifier;
    verifier.add_builtin("preserve_existing_identity");

    World left{"seed"};
    World right{"seed"};
    seed_types(left);
    seed_types(right);
    REQUIRE(left.commit(left.object_delta("A", "Node"), verifier).accepted);
    REQUIRE(right.commit(right.object_delta("A", "Node"), verifier).accepted);

    const auto node_left = left.type_id("Node");
    const auto observation = left.type_id("Observation");
    const auto region = left.type_id("Region");
    const auto archive = left.type_id("Archive");
    auto f = std::make_shared<DefinedMorphism>("f", MorphismSignature{node_left, observation});
    auto g = std::make_shared<DefinedMorphism>("g", MorphismSignature{observation, region});
    auto h = std::make_shared<DefinedMorphism>("h", MorphismSignature{region, archive});

    auto gf = compose(g, f);
    REQUIRE(gf.has_value());
    auto h_gf = compose(h, *gf);
    REQUIRE(h_gf.has_value());

    auto hg = compose(h, g);
    REQUIRE(hg.has_value());
    auto hg_f = compose(*hg, f);
    REQUIRE(hg_f.has_value());

    const auto left_result = left.commit((*h_gf)->apply(left.snapshot(), Selection{{left.object_by_name("A")}, {}}), verifier);
    const auto right_result = right.commit((*hg_f)->apply(right.snapshot(), Selection{{right.object_by_name("A")}, {}}), verifier);

    const Observer observer{"test"};
    REQUIRE(left_result.accepted == right_result.accepted);
    REQUIRE(left.hash() == right.hash());
    REQUIRE(left.snapshot().structural_hash() == right.snapshot().structural_hash());
    REQUIRE(law_status_class(left_result) == law_status_class(right_result));
    REQUIRE(observer.inspect_graph(left.snapshot()).body == observer.inspect_graph(right.snapshot()).body);
    REQUIRE(normalized_morphism_path(left_result.events) == normalized_morphism_path(right_result.events));
}
