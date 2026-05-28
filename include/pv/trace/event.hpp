// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>
#include <string>

#include "pv/core/id.hpp"

namespace pv {

struct TraceEvent {
    Epoch epoch;
    std::string event;
    std::map<std::string, std::string> fields;
    std::map<std::string, double> measurements;
};

}  // namespace pv
