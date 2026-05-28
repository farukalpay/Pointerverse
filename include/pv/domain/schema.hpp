// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

namespace pv {

struct DomainSchema {
    std::vector<std::string> object_types;
    std::vector<std::string> relations;
};

}  // namespace pv
