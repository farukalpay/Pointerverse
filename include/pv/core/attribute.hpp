// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#include "pv/core/value.hpp"

namespace pv {

struct Attribute {
    std::string key;
    Value value;
};

[[nodiscard]] bool operator==(const Attribute& left, const Attribute& right) noexcept;
[[nodiscard]] bool operator<(const Attribute& left, const Attribute& right) noexcept;
void sort_attributes(std::vector<Attribute>& attributes);

}  // namespace pv
