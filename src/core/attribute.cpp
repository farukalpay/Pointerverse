// SPDX-License-Identifier: Apache-2.0
#include "pv/core/attribute.hpp"

#include <algorithm>

namespace pv {

bool operator==(const Attribute& left, const Attribute& right) noexcept {
    return left.key == right.key && left.value == right.value;
}

bool operator<(const Attribute& left, const Attribute& right) noexcept {
    if (left.key != right.key) {
        return left.key < right.key;
    }
    return left.value < right.value;
}

void sort_attributes(std::vector<Attribute>& attributes) {
    std::ranges::sort(attributes, [](const Attribute& left, const Attribute& right) {
        return left < right;
    });
}

}  // namespace pv
