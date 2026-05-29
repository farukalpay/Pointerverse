// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <limits>

#include "pv/kernel/canonical_codec.hpp"
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

TEST_CASE("coordinate risk lattice join is associative commutative and idempotent") {
    const RiskLatticeElement a{{RiskCoordinate{"structural", "forward_cone_mass", 1}, RiskCoordinate{"law", "total_magnitude", 4}}};
    const RiskLatticeElement b{{RiskCoordinate{"structural", "forward_cone_mass", 3}, RiskCoordinate{"repair", "distance", 2}}};
    const RiskLatticeElement c{{RiskCoordinate{"surprise", "history_distance", 8}, RiskCoordinate{"law", "total_magnitude", 1}}};

    REQUIRE(join(a, b) == join(b, a));
    REQUIRE(join(join(a, b), c) == join(a, join(b, c)));
    REQUIRE(join(a, a) == canonical_risk_lattice(a));
    REQUIRE(less_equal(a, join(a, b)));
}

TEST_CASE("coordinate order does not change risk lattice hash") {
    const RiskLatticeElement left{{
        RiskCoordinate{"structural", "forward_cone_mass", 9},
        RiskCoordinate{"repair", "distance", 1},
        RiskCoordinate{"law", "total_magnitude", 2}
    }};
    const RiskLatticeElement right{{
        RiskCoordinate{"law", "total_magnitude", 2},
        RiskCoordinate{"structural", "forward_cone_mass", 9},
        RiskCoordinate{"repair", "distance", 1}
    }};

    REQUIRE(risk_lattice_hash(left) == risk_lattice_hash(right));
}

TEST_CASE("risk projection uses saturating integer arithmetic") {
    const RiskVector risk{std::numeric_limits<std::uint64_t>::max(), 1, 1, 1};
    const RiskProjection projection{2, 1, 1, 1};

    REQUIRE(project(risk, projection) == std::numeric_limits<std::uint64_t>::max());
}

TEST_CASE("component projection policy changes projection hash without changing measurement hash") {
    Hash256 measurement;
    measurement.value.back() = std::byte{0x42};
    const RiskLatticeElement lattice{{
        RiskCoordinate{"structural", "forward_cone_mass", 10},
        RiskCoordinate{"law", "total_magnitude", 2}
    }};
    auto left_policy = default_projection_policy();
    auto right_policy = left_policy;
    for (auto& term : right_policy.terms) {
        if (term.namespace_id == "structural" && term.component_id == "forward_cone_mass") {
            term.weight_num = 3;
        }
    }

    const auto left = make_projection_result(measurement, lattice, left_policy);
    const auto right = make_projection_result(measurement, lattice, right_policy);

    REQUIRE(left.measurement_hash == right.measurement_hash);
    REQUIRE(left.projection_policy_hash != right.projection_policy_hash);
    REQUIRE(left.projected_score != right.projected_score);
    REQUIRE(left.projection_hash != right.projection_hash);
}

TEST_CASE("calibrated projection is monotone over the risk lattice") {
    ProjectionPolicy policy;
    policy.terms.push_back(ProjectionTerm{
        "structural",
        "forward_cone_mass",
        1,
        1,
        "robust_z",
        10,
        2,
        12,
        20,
        30
    });

    const RiskLatticeElement low{{RiskCoordinate{"structural", "forward_cone_mass", 12}}};
    const RiskLatticeElement high{{RiskCoordinate{"structural", "forward_cone_mass", 30}}};

    REQUIRE(less_equal(low, high));
    REQUIRE(project(low, policy) <= project(high, policy));
    REQUIRE(projection_policy_hash(policy) == projection_policy_hash(decode_projection_policy_bytes(canonical_encode(policy))));
}
