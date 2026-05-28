#include "pointerverse/types.hpp"

#include <algorithm>
#include <cctype>
#include <fmt/format.h>

namespace pointerverse {
namespace {

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

}  // namespace

bool ObjectHandle::is_valid_token() const noexcept {
    return slot != invalid_slot && generation != 0;
}

bool operator==(ObjectHandle lhs, ObjectHandle rhs) noexcept {
    return lhs.slot == rhs.slot && lhs.generation == rhs.generation;
}

bool operator!=(ObjectHandle lhs, ObjectHandle rhs) noexcept {
    return !(lhs == rhs);
}

bool operator==(RelationId lhs, RelationId rhs) noexcept {
    return lhs.value == rhs.value;
}

bool operator==(MorphismId lhs, MorphismId rhs) noexcept {
    return lhs.value == rhs.value;
}

bool operator==(RegionId lhs, RegionId rhs) noexcept {
    return lhs.value == rhs.value;
}

std::string to_string(ObjectHandle handle) {
    if (!handle.is_valid_token()) {
        return "object#invalid";
    }
    return fmt::format("object#{}:{}", handle.slot, handle.generation);
}

std::string to_string(RelationId id) {
    return fmt::format("relation#{}", id.value);
}

std::string to_string(MorphismId id) {
    return fmt::format("morphism#{}", id.value);
}

std::string to_string(RegionId id) {
    return fmt::format("R{}", id.value);
}

std::string to_string(CausalTag tag) {
    switch (tag) {
        case CausalTag::Structural:
            return "structural";
        case CausalTag::Causal:
            return "causal";
        case CausalTag::Observational:
            return "observational";
        case CausalTag::Emergent:
            return "emergent";
        case CausalTag::Contradictory:
            return "contradictory";
        case CausalTag::Unknown:
            return "unknown";
    }
    return "unknown";
}

CausalTag causal_tag_from_string(const std::string& value) {
    const auto key = lowercase(value);
    if (key == "structural") {
        return CausalTag::Structural;
    }
    if (key == "causal" || key == "causes") {
        return CausalTag::Causal;
    }
    if (key == "observational" || key == "observes") {
        return CausalTag::Observational;
    }
    if (key == "emergent" || key == "emerges") {
        return CausalTag::Emergent;
    }
    if (key == "contradictory" || key == "contradicts") {
        return CausalTag::Contradictory;
    }
    return CausalTag::Unknown;
}

}  // namespace pointerverse
