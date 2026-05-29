// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "pv/intervention/scale_value.hpp"

using namespace pv;

TEST_CASE("scale values are exact normalized dyadic rationals") {
    const auto zero = ScaleValue::zero();
    const auto half = ScaleValue::dyadic(2, 2);
    const auto one = ScaleValue::one();

    REQUIRE(to_string(zero) == "0");
    REQUIRE(to_string(half) == "1/2");
    REQUIRE(to_string(one) == "1");
    REQUIRE(zero < half);
    REQUIRE(half < one);
    REQUIRE(parse_scale_value("3/8") == ScaleValue::dyadic(3, 3));
    REQUIRE_FALSE(parse_scale_value("1/3").has_value());
    REQUIRE(scale_value_hash(half) == scale_value_hash(ScaleValue::dyadic(1, 1)));
}

TEST_CASE("dyadic refinement is deterministic") {
    const auto first = dyadic_refinement_scales(2);
    const auto second = dyadic_refinement_scales(2);

    REQUIRE(first == second);
    REQUIRE(first.size() == 5);
    REQUIRE(to_string(first[0]) == "0");
    REQUIRE(to_string(first[1]) == "1/4");
    REQUIRE(to_string(first[2]) == "1/2");
    REQUIRE(to_string(first[3]) == "3/4");
    REQUIRE(to_string(first[4]) == "1");
}
