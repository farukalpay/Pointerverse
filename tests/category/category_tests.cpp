// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "pv/category/composition.hpp"
#include "pv/core/world.hpp"
#include "pv/observer/observer.hpp"

using namespace pv;

TEST_CASE("composition checks domain and codomain") {
    World world{"seed"};
    const auto node = world.type_id("Node");
    const auto observation = world.type_id("Observation");
    const auto region = world.type_id("Region");

    DefinedMorphism stabilize{"Stabilize", MorphismSignature{node, node}};
    DefinedMorphism readout{"Readout", MorphismSignature{node, observation}};
    DefinedMorphism compress{"Compress", MorphismSignature{region, node}};

    const auto valid = compose(readout, stabilize);
    REQUIRE(valid.has_value());
    REQUIRE(valid->signature().domain == node);
    REQUIRE(valid->signature().codomain == observation);

    const auto invalid = compose(compress, stabilize);
    REQUIRE_FALSE(invalid.has_value());
    REQUIRE(invalid.error() == CompositionError::DomainCodomainMismatch);
}

TEST_CASE("identity composition preserves signatures") {
    World world{"seed"};
    const auto node = world.type_id("Node");
    DefinedMorphism f{"Translate", MorphismSignature{node, node}};
    IdentityMorphism id{node};

    const auto left = compose(f, id);
    const auto right = compose(id, f);

    REQUIRE(left.has_value());
    REQUIRE(right.has_value());
    REQUIRE(left->signature() == f.signature());
    REQUIRE(right->signature() == f.signature());
}

TEST_CASE("associative paths agree by hash, law status, and observer projection") {
    Verifier verifier;
    verifier.add_builtin("preserve_existing_identity");

    World left{"seed"};
    World right{"seed"};
    REQUIRE(left.commit(left.object_delta("A", "Node"), verifier).accepted);
    REQUIRE(right.commit(right.object_delta("A", "Node"), verifier).accepted);

    const auto node_left = left.type_id("Node");
    DefinedMorphism f{"f", MorphismSignature{node_left, node_left}};
    DefinedMorphism g{"g", MorphismSignature{node_left, node_left}};
    DefinedMorphism h{"h", MorphismSignature{node_left, node_left}};

    auto gf = compose(g, f);
    REQUIRE(gf.has_value());
    auto h_gf = compose(h, *gf);
    REQUIRE(h_gf.has_value());

    auto hg = compose(h, g);
    REQUIRE(hg.has_value());
    auto hg_f = compose(*hg, f);
    REQUIRE(hg_f.has_value());

    REQUIRE(left.commit(h_gf->apply(left.snapshot(), Selection{{left.object_by_name("A")}, {}}), verifier).accepted);
    REQUIRE(right.commit(hg_f->apply(right.snapshot(), Selection{{right.object_by_name("A")}, {}}), verifier).accepted);

    const Observer observer{"test"};
    REQUIRE(left.hash() == right.hash());
    REQUIRE(left.trace().events().back().event == right.trace().events().back().event);
    REQUIRE(observer.inspect_graph(left.snapshot()).body == observer.inspect_graph(right.snapshot()).body);
}
