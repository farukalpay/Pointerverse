// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <limits>
#include <string>

namespace pv {

using ObjectIndex = std::uint32_t;
using Generation = std::uint32_t;

struct WorldId {
    std::uint64_t value{1};

    friend bool operator==(WorldId, WorldId) = default;
};

struct Epoch {
    std::uint64_t value{0};

    friend bool operator==(Epoch, Epoch) = default;
    friend auto operator<=>(Epoch, Epoch) = default;
};

struct ObjectId {
    static constexpr ObjectIndex invalid_index = std::numeric_limits<ObjectIndex>::max();

    ObjectIndex index{invalid_index};
    Generation generation{0};

    [[nodiscard]] bool valid_token() const noexcept;

    friend bool operator==(ObjectId, ObjectId) = default;
};

struct QualifiedObject {
    WorldId world;
    Epoch epoch;
    ObjectId object;

    friend bool operator==(QualifiedObject, QualifiedObject) = default;
};

[[nodiscard]] std::string to_string(WorldId id);
[[nodiscard]] std::string to_string(Epoch epoch);
[[nodiscard]] std::string to_string(ObjectId id);
[[nodiscard]] std::string to_string(QualifiedObject object);

}  // namespace pv
