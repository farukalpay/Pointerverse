// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#include "pv/decision/signal.hpp"

namespace pv {

struct Recommendation {
    std::string id;
    std::string priority;
    std::string action;
    std::string reason;
    std::vector<Signal> signals;

    friend bool operator==(const Recommendation&, const Recommendation&) = default;
};

}  // namespace pv
