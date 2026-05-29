// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string_view>

#include "pv/law/law.hpp"

namespace pv {

[[deprecated("Use measured risk functional instead.")]]
[[nodiscard]] int risk_points(Severity severity) noexcept;
[[deprecated("Use measured risk functional instead.")]]
[[nodiscard]] int risk_points(std::string_view severity) noexcept;

}  // namespace pv
