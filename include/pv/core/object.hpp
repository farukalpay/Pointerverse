// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "pv/core/handle.hpp"
#include "pv/core/type.hpp"

namespace pv {

enum class ExistenceState : std::uint8_t {
    Alive,
    Dormant,
    Collapsed,
    Tombstoned
};

struct Object {
    ObjectId id;
    std::string name;
    TypeId type;
    ExistenceState existence{ExistenceState::Alive};
    std::unordered_map<std::string, std::string> attributes;
};

using ObjectHandle = Handle<Object>;

class ObjectArena {
public:
    [[nodiscard]] ObjectId create(std::string name, TypeId type, ExistenceState existence = ExistenceState::Alive);
    [[nodiscard]] bool contains(ObjectId id) const noexcept;
    [[nodiscard]] const Object& get(ObjectId id) const;
    [[nodiscard]] Object& get(ObjectId id);
    [[nodiscard]] const std::vector<Object>& objects() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    void set_existence(ObjectId id, ExistenceState state);
    void set_type(ObjectId id, TypeId type);
    void restore(std::vector<Object> objects);

private:
    std::vector<Object> objects_;
};

[[nodiscard]] std::string to_string(ExistenceState state);
[[nodiscard]] ExistenceState existence_state_from_string(std::string_view value);

}  // namespace pv
