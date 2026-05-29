// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pv/measure/risk_value.hpp"

namespace pv {

[[nodiscard]] RiskVector join(RiskVector left, RiskVector right) noexcept;
[[nodiscard]] bool less_equal(RiskVector left, RiskVector right) noexcept;

}  // namespace pv

