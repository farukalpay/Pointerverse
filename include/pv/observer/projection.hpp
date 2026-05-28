// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>
#include <string>

namespace pv {

struct Projection {
    std::string observer;
    std::string target;
    std::string body;
    std::map<std::string, double> measurements;
};

}  // namespace pv
