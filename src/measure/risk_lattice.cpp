// SPDX-License-Identifier: Apache-2.0
#include "pv/measure/risk_lattice.hpp"

#include <algorithm>
#include <utility>

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

RiskLatticeElement join(RiskLatticeElement left, RiskLatticeElement right) {
    left = canonical_risk_lattice(std::move(left));
    right = canonical_risk_lattice(std::move(right));
    left.coordinates.insert(left.coordinates.end(), right.coordinates.begin(), right.coordinates.end());
    return canonical_risk_lattice(std::move(left));
}

bool less_equal(const RiskLatticeElement& left, const RiskLatticeElement& right) noexcept {
    for (const auto& coordinate : left.coordinates) {
        if (coordinate.value > coordinate_value(right, coordinate.namespace_id, coordinate.component_id)) {
            return false;
        }
    }
    return true;
}

}  // namespace pv
