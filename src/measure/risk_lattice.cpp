// SPDX-License-Identifier: Apache-2.0
#include "pv/measure/risk_lattice.hpp"

#include <algorithm>

namespace pv {

RiskVector join(RiskVector left, RiskVector right) noexcept {
    return RiskVector{
        std::max(left.structural, right.structural),
        std::max(left.law_distance, right.law_distance),
        std::max(left.repair_distance, right.repair_distance),
        std::max(left.surprise, right.surprise)
    };
}

bool less_equal(RiskVector left, RiskVector right) noexcept {
    return left.structural <= right.structural
        && left.law_distance <= right.law_distance
        && left.repair_distance <= right.repair_distance
        && left.surprise <= right.surprise;
}

}  // namespace pv

