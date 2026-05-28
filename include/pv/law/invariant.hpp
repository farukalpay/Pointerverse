// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <string_view>

#include "pv/law/law.hpp"

namespace pv {

[[nodiscard]] std::shared_ptr<Law> make_builtin_law(std::string_view name, double tolerance = 1e-9);

}  // namespace pv
