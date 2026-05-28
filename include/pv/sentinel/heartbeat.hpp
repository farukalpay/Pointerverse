// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>

#include "pv/hash/canonical.hpp"

namespace pv {

struct Heartbeat {
    std::string worker;
    std::uint64_t tick{0};
    Hash256 last_measurement;
    bool healthy{true};
};

}  // namespace pv
