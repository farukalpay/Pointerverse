// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pv {

struct TypeId {
    std::uint32_t value{0};

    [[nodiscard]] bool valid() const noexcept { return value != 0; }

    friend bool operator==(TypeId, TypeId) = default;
};

class TypeRegistry {
public:
    [[nodiscard]] TypeId intern(std::string_view name);
    [[nodiscard]] std::optional<TypeId> find(std::string_view name) const;
    [[nodiscard]] const std::string& name(TypeId id) const;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] const std::vector<std::string>& names() const noexcept;

private:
    std::unordered_map<std::string, TypeId> ids_;
    std::vector<std::string> names_;
};

[[nodiscard]] std::string to_string(TypeId id);

}  // namespace pv
