// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>

#include "pv/hash/canonical.hpp"
#include "pv/measure/risk_value.hpp"

namespace pv {

struct RiskProjection {
    std::uint64_t structural_weight{1};
    std::uint64_t law_weight{1};
    std::uint64_t repair_weight{1};
    std::uint64_t surprise_weight{1};
};

struct ProjectionResult {
    Hash256 measurement_hash;
    Hash256 projection_policy_hash;
    std::uint64_t projected_score{0};
    std::string decision;
    Hash256 projection_hash;
};

[[nodiscard]] std::uint64_t project(RiskVector value, RiskProjection projection = {}) noexcept;
[[nodiscard]] Hash256 projection_policy_hash(RiskProjection projection);
[[nodiscard]] Hash256 projection_result_hash(const ProjectionResult& result);
[[nodiscard]] ProjectionResult make_projection_result(
    Hash256 measurement_hash,
    RiskVector value,
    RiskProjection projection,
    std::string decision = {});

}  // namespace pv
