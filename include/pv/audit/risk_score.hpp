// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string_view>

#include "pv/law/law.hpp"

namespace pv {

[[nodiscard]] int risk_points(Severity severity) noexcept;
[[nodiscard]] int risk_points(std::string_view severity) noexcept;

}  // namespace pv
