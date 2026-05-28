#pragma once

#include <complex>
#include <cstddef>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace pointerverse {

using Scalar = std::complex<double>;

class StateVector {
public:
    StateVector() = default;
    explicit StateVector(std::vector<Scalar> amplitudes);

    [[nodiscard]] static StateVector basis(std::size_t dimension, std::size_t index = 0);

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t dimension() const noexcept;
    [[nodiscard]] const std::vector<Scalar>& amplitudes() const noexcept;
    [[nodiscard]] std::vector<Scalar>& amplitudes() noexcept;

    [[nodiscard]] double norm_squared() const;
    [[nodiscard]] double norm() const;
    [[nodiscard]] double normalization_error() const;
    [[nodiscard]] bool is_normalized(double tolerance = 1e-9) const;
    [[nodiscard]] std::vector<double> probabilities() const;
    [[nodiscard]] double entropy() const;
    [[nodiscard]] std::string summary(std::size_t max_terms = 4) const;
    [[nodiscard]] nlohmann::json to_json() const;

    bool normalize(double epsilon = 1e-12);

private:
    std::vector<Scalar> amplitudes_;
};

}  // namespace pointerverse
