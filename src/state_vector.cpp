#include "pointerverse/state_vector.hpp"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace pointerverse {
namespace {

std::string format_scalar(Scalar value) {
    const auto real = value.real();
    const auto imag = value.imag();
    const auto sign = imag < 0.0 ? "-" : "+";
    return fmt::format("{:.6g}{}{:.6g}i", real, sign, std::abs(imag));
}

}  // namespace

StateVector::StateVector(std::vector<Scalar> amplitudes) : amplitudes_(std::move(amplitudes)) {}

StateVector StateVector::basis(std::size_t dimension, std::size_t index) {
    if (dimension == 0) {
        throw std::invalid_argument("StateVector::basis requires a non-zero dimension");
    }
    if (index >= dimension) {
        throw std::out_of_range("StateVector::basis index is outside the dimension");
    }

    std::vector<Scalar> amplitudes(dimension, Scalar{0.0, 0.0});
    amplitudes[index] = Scalar{1.0, 0.0};
    return StateVector{std::move(amplitudes)};
}

bool StateVector::empty() const noexcept {
    return amplitudes_.empty();
}

std::size_t StateVector::dimension() const noexcept {
    return amplitudes_.size();
}

const std::vector<Scalar>& StateVector::amplitudes() const noexcept {
    return amplitudes_;
}

std::vector<Scalar>& StateVector::amplitudes() noexcept {
    return amplitudes_;
}

double StateVector::norm_squared() const {
    return std::accumulate(amplitudes_.begin(), amplitudes_.end(), 0.0, [](double sum, const Scalar& value) {
        return sum + std::norm(value);
    });
}

double StateVector::norm() const {
    return std::sqrt(norm_squared());
}

double StateVector::normalization_error() const {
    if (empty()) {
        return 0.0;
    }
    return std::abs(1.0 - norm_squared());
}

bool StateVector::is_normalized(double tolerance) const {
    return normalization_error() <= tolerance;
}

std::vector<double> StateVector::probabilities() const {
    std::vector<double> result;
    result.reserve(amplitudes_.size());
    for (const auto& amplitude : amplitudes_) {
        result.push_back(std::norm(amplitude));
    }
    return result;
}

double StateVector::entropy() const {
    double result = 0.0;
    for (const auto probability : probabilities()) {
        if (probability > 0.0) {
            result -= probability * std::log2(probability);
        }
    }
    return result;
}

std::string StateVector::summary(std::size_t max_terms) const {
    if (amplitudes_.empty()) {
        return "[]";
    }

    std::ostringstream output;
    output << "[";
    const auto visible_terms = std::min(max_terms, amplitudes_.size());
    for (std::size_t index = 0; index < visible_terms; ++index) {
        if (index != 0) {
            output << ", ";
        }
        output << format_scalar(amplitudes_[index]);
    }
    if (visible_terms < amplitudes_.size()) {
        output << ", ...";
    }
    output << "]";
    return output.str();
}

nlohmann::json StateVector::to_json() const {
    nlohmann::json amplitudes = nlohmann::json::array();
    for (const auto& amplitude : amplitudes_) {
        amplitudes.push_back({
            {"real", amplitude.real()},
            {"imag", amplitude.imag()},
            {"probability", std::norm(amplitude)}
        });
    }

    return {
        {"dimension", dimension()},
        {"norm", norm()},
        {"normalization_error", normalization_error()},
        {"entropy", entropy()},
        {"amplitudes", std::move(amplitudes)}
    };
}

bool StateVector::normalize(double epsilon) {
    const auto current_norm = norm();
    if (current_norm <= epsilon) {
        return false;
    }
    for (auto& amplitude : amplitudes_) {
        amplitude /= current_norm;
    }
    return true;
}

}  // namespace pointerverse
