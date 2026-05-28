#pragma once

#include <functional>
#include <string>

#include "pointerverse/trace.hpp"

namespace pointerverse {

using LawCheck = std::function<LawResult(const WorldSnapshot& before, const WorldSnapshot& after)>;

struct Law {
    std::string name;
    LawCheck check;
};

}  // namespace pointerverse
