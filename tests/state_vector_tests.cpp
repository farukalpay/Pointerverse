#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "pointerverse/state_vector.hpp"

using pointerverse::Scalar;
using pointerverse::StateVector;

TEST_CASE("state vectors normalize complex amplitudes") {
    StateVector state{{Scalar{3.0, 0.0}, Scalar{0.0, 4.0}}};

    REQUIRE(state.normalize());
    REQUIRE(state.norm() == Catch::Approx(1.0));
    REQUIRE(state.normalization_error() == Catch::Approx(0.0).margin(1e-12));

    const auto probabilities = state.probabilities();
    REQUIRE(probabilities.size() == 2);
    REQUIRE(probabilities[0] == Catch::Approx(0.36));
    REQUIRE(probabilities[1] == Catch::Approx(0.64));
}

TEST_CASE("basis states have unit probability mass") {
    const auto state = StateVector::basis(3, 2);

    REQUIRE(state.dimension() == 3);
    REQUIRE(state.norm_squared() == Catch::Approx(1.0));
    REQUIRE(state.probabilities()[2] == Catch::Approx(1.0));
}
