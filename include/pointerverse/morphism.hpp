#pragma once

#include <string>
#include <vector>

#include "pointerverse/types.hpp"

namespace pointerverse {

struct Morphism {
    MorphismId id;
    std::string name;
    std::string from_type;
    std::string to_type;
    std::string effect;
};

struct CompositionResult {
    bool valid{false};
    bool weakly_valid{false};
    std::string name;
    std::string from_type;
    std::string to_type;
    double law_residual_delta{0.0};
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

}  // namespace pointerverse
