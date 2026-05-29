// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pv/measure/risk_value.hpp"

namespace pv {

[[nodiscard]] RiskVector join(RiskVector left, RiskVector right) noexcept;
[[nodiscard]] bool less_equal(RiskVector left, RiskVector right) noexcept;
[[nodiscard]] RiskLatticeElement join(RiskLatticeElement left, RiskLatticeElement right);
[[nodiscard]] bool less_equal(const RiskLatticeElement& left, const RiskLatticeElement& right) noexcept;

}  // namespace pv
