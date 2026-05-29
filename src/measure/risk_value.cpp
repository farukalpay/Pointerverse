// SPDX-License-Identifier: Apache-2.0
#include "pv/measure/risk_value.hpp"

namespace pv {

bool empty(RiskVector value) noexcept {
    return value.structural == 0
        && value.law_distance == 0
        && value.repair_distance == 0
        && value.surprise == 0;
}

}  // namespace pv

