// SPDX-License-Identifier: Apache-2.0
#include "pv/core/id.hpp"

#include <fmt/format.h>

namespace pv {

bool ObjectId::valid_token() const noexcept {
    return index != invalid_index && generation != 0;
}

std::string to_string(WorldId id) {
    return fmt::format("W{}", id.value);
}

std::string to_string(Epoch epoch) {
    return fmt::format("E{}", epoch.value);
}

std::string to_string(ObjectId id) {
    return id.valid_token() ? fmt::format("O{}:{}", id.index, id.generation) : "O<invalid>";
}

std::string to_string(QualifiedObject object) {
    return fmt::format("{}@{}:{}", to_string(object.world), to_string(object.epoch), to_string(object.object));
}

}  // namespace pv
