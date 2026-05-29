// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <limits>

#include "pv/measure/risk_lattice.hpp"
#include "pv/measure/risk_projection.hpp"

using namespace pv;

TEST_CASE("risk join is associative commutative and idempotent") {
    const RiskVector a{1, 4, 2, 0};
    const RiskVector b{3, 1, 2, 8};
    const RiskVector c{2, 7, 5, 1};

    REQUIRE(join(a, b) == join(b, a));
    REQUIRE(join(join(a, b), c) == join(a, join(b, c)));
    REQUIRE(join(a, a) == a);
    REQUIRE(less_equal(a, join(a, b)));
}

TEST_CASE("risk projection uses saturating integer arithmetic") {
    const RiskVector risk{std::numeric_limits<std::uint64_t>::max(), 1, 1, 1};
    const RiskProjection projection{2, 1, 1, 1};

    REQUIRE(project(risk, projection) == std::numeric_limits<std::uint64_t>::max());
}

