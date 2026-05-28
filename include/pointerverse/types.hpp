#pragma once

#include <cstdint>
#include <limits>
#include <string>

namespace pointerverse {

struct ObjectHandle {
    static constexpr std::uint32_t invalid_slot = std::numeric_limits<std::uint32_t>::max();

    std::uint32_t slot{invalid_slot};
    std::uint32_t generation{0};

    [[nodiscard]] bool is_valid_token() const noexcept;
};

struct RelationId {
    std::uint64_t value{0};
};

struct MorphismId {
    std::uint64_t value{0};
};

struct RegionId {
    std::uint64_t value{0};
};

enum class CausalTag {
    Structural,
    Causal,
    Observational,
    Emergent,
    Contradictory,
    Unknown
};

[[nodiscard]] bool operator==(ObjectHandle lhs, ObjectHandle rhs) noexcept;
[[nodiscard]] bool operator!=(ObjectHandle lhs, ObjectHandle rhs) noexcept;
[[nodiscard]] bool operator==(RelationId lhs, RelationId rhs) noexcept;
[[nodiscard]] bool operator==(MorphismId lhs, MorphismId rhs) noexcept;
[[nodiscard]] bool operator==(RegionId lhs, RegionId rhs) noexcept;

[[nodiscard]] std::string to_string(ObjectHandle handle);
[[nodiscard]] std::string to_string(RelationId id);
[[nodiscard]] std::string to_string(MorphismId id);
[[nodiscard]] std::string to_string(RegionId id);
[[nodiscard]] std::string to_string(CausalTag tag);
[[nodiscard]] CausalTag causal_tag_from_string(const std::string& value);

}  // namespace pointerverse
