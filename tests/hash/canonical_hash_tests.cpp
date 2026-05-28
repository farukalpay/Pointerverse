// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

#include "pv/core/world.hpp"
#include "pv/hash/hasher.hpp"

using namespace pv;

TEST_CASE("canonical hash is stable under snapshot insertion order normalization") {
    World world{"seed"};
    Verifier verifier;
    REQUIRE(world.commit(world.object_delta("A", "Node"), verifier).accepted);
    REQUIRE(world.commit(world.object_delta("B", "Node"), verifier).accepted);
    REQUIRE(world.commit(world.object_delta("C", "Node"), verifier).accepted);
    REQUIRE(world.commit(world.link_delta(
        world.object_by_name("A"),
        world.object_by_name("B"),
        "causes",
        0.7,
        CausalRole::Structural), verifier).accepted);
    REQUIRE(world.commit(world.link_delta(
        world.object_by_name("B"),
        world.object_by_name("C"),
        "causes",
        0.4,
        CausalRole::Structural), verifier).accepted);

    auto reordered = world.snapshot();
    std::ranges::reverse(reordered.objects);
    std::ranges::reverse(reordered.pointers);

    REQUIRE(reordered.canonical_hash() == world.snapshot().canonical_hash());
    REQUIRE(reordered.structural_hash() == world.snapshot().structural_hash());
}

TEST_CASE("canonical double hashing normalizes NaN and signed zero") {
    REQUIRE(canonical_f64(0.0) == canonical_f64(-0.0));
    REQUIRE(canonical_f64(std::numeric_limits<double>::quiet_NaN()) == canonical_f64(std::nan("1")));

    CanonicalHasher positive_zero;
    positive_zero.write_f64(0.0);
    CanonicalHasher negative_zero;
    negative_zero.write_f64(-0.0);
    REQUIRE(positive_zero.finish() == negative_zero.finish());

    CanonicalHasher quiet_nan;
    quiet_nan.write_f64(std::numeric_limits<double>::quiet_NaN());
    CanonicalHasher payload_nan;
    payload_nan.write_f64(std::nan("42"));
    REQUIRE(quiet_nan.finish() == payload_nan.finish());
}
