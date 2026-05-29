// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "pv/measure/intrinsic_edit_cost.hpp"

using namespace pv;

TEST_CASE("canonical edit cost ignores whitespace and comments") {
    const auto left = IntrinsicEditCost{}.measure(
        "# generated repair\n"
        "unlink   A   ->   B : causes   pointer=P2\n");
    const auto right = IntrinsicEditCost{}.measure("unlink A->B:causes pointer = P2 # same repair\n");

    REQUIRE(left.value == right.value);
    REQUIRE(left.tokens == right.tokens);
    REQUIRE(left.canonical_script == right.canonical_script);
    REQUIRE(left.canonical_script == "unlink A -> B : causes pointer = P2");
}

TEST_CASE("canonical edit cost is deterministic for empty scripts") {
    const auto measured = IntrinsicEditCost{}.measure(" \n # comment only\n");

    REQUIRE(measured.value == 0);
    REQUIRE(measured.tokens == 0);
    REQUIRE(measured.canonical_script.empty());
}
