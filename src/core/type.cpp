// SPDX-License-Identifier: Apache-2.0
#include "pv/core/type.hpp"

#include <algorithm>
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

TypeId TypeRegistry::intern_at(TypeId id, std::string_view name) {
    if (name.empty()) {
        throw std::invalid_argument("type name cannot be empty");
    }
    if (!id.valid()) {
        return intern(name);
    }

    const std::string key{name};
    if (const auto existing = find(key); existing.has_value()) {
        if (*existing != id) {
            throw std::invalid_argument(fmt::format("type '{}' already exists as {}", key, to_string(*existing)));
        }
        return *existing;
    }

    if (names_.size() < id.value) {
        names_.resize(id.value);
    }
    auto& slot = names_[id.value - 1];
    if (!slot.empty() && slot != key) {
        throw std::invalid_argument(fmt::format("type id {} already maps to '{}'", to_string(id), slot));
    }
    slot = key;
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

void TypeRegistry::restore_names(const std::map<std::uint32_t, std::string>& names) {
    ids_.clear();
    names_.clear();

    std::uint32_t max_id = 0;
    for (const auto& [id, _] : names) {
        max_id = std::max(max_id, id);
    }
    names_.resize(max_id);
    for (const auto& [id, name] : names) {
        if (id == 0) {
            continue;
        }
        names_[id - 1] = name;
        if (!name.empty()) {
            ids_.emplace(name, TypeId{id});
        }
    }
}

std::string to_string(TypeId id) {
    return id.valid() ? fmt::format("T{}", id.value) : "T<invalid>";
}

}  // namespace pv
