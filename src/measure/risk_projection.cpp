// SPDX-License-Identifier: Apache-2.0
#include "pv/measure/risk_projection.hpp"

#include <limits>

namespace pv {
namespace {

std::uint64_t saturating_mul(std::uint64_t left, std::uint64_t right) noexcept {
    if (left == 0 || right == 0) {
        return 0;
    }
    constexpr auto max = std::numeric_limits<std::uint64_t>::max();
    if (left > max / right) {
        return max;
    }
    return left * right;
}

std::uint64_t saturating_add(std::uint64_t left, std::uint64_t right) noexcept {
    constexpr auto max = std::numeric_limits<std::uint64_t>::max();
    if (max - left < right) {
        return max;
    }
    return left + right;
}

}  // namespace

std::uint64_t project(RiskVector value, RiskProjection projection) noexcept {
    std::uint64_t out = 0;
    out = saturating_add(out, saturating_mul(value.structural, projection.structural_weight));
    out = saturating_add(out, saturating_mul(value.law_distance, projection.law_weight));
    out = saturating_add(out, saturating_mul(value.repair_distance, projection.repair_weight));
    out = saturating_add(out, saturating_mul(value.surprise, projection.surprise_weight));
    return out;
}

}  // namespace pv

