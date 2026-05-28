// SPDX-License-Identifier: Apache-2.0
#include "pv/core/type.hpp"

#include <fmt/format.h>
#include <stdexcept>

namespace pv {

TypeId TypeRegistry::intern(std::string_view name) {
    if (name.empty()) {
        throw std::invalid_argument("type name cannot be empty");
    }

    const std::string key{name};
    if (const auto iter = ids_.find(key); iter != ids_.end()) {
        return iter->second;
    }

    const TypeId id{static_cast<std::uint32_t>(names_.size() + 1)};
    names_.push_back(key);
    ids_.emplace(key, id);
    return id;
}

std::optional<TypeId> TypeRegistry::find(std::string_view name) const {
    const auto iter = ids_.find(std::string{name});
    if (iter == ids_.end()) {
        return std::nullopt;
    }
    return iter->second;
}

const std::string& TypeRegistry::name(TypeId id) const {
    if (!id.valid() || id.value > names_.size()) {
        throw std::out_of_range(fmt::format("unknown type {}", to_string(id)));
    }
    return names_[id.value - 1];
}

std::size_t TypeRegistry::size() const noexcept {
    return names_.size();
}

const std::vector<std::string>& TypeRegistry::names() const noexcept {
    return names_;
}

std::string to_string(TypeId id) {
    return id.valid() ? fmt::format("T{}", id.value) : "T<invalid>";
}

}  // namespace pv
