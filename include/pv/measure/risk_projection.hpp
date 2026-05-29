// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

#include "pv/measure/risk_value.hpp"

namespace pv {

struct RiskProjection {
    std::uint64_t structural_weight{1};
    std::uint64_t law_weight{1};
    std::uint64_t repair_weight{1};
    std::uint64_t surprise_weight{1};
};

[[nodiscard]] std::uint64_t project(RiskVector value, RiskProjection projection = {}) noexcept;

}  // namespace pv

