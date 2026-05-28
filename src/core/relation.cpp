// SPDX-License-Identifier: Apache-2.0
#include "pv/core/relation.hpp"

#include <algorithm>
#include <cctype>
#include <fmt/format.h>
#include <stdexcept>

namespace pv {
namespace {

std::string normalized(std::string_view value) {
    std::string out{value};
    std::ranges::transform(out, out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

}  // namespace

RelationType RelationRegistry::intern(std::string_view name) {
    if (name.empty()) {
        throw std::invalid_argument("relation name cannot be empty");
    }

    const std::string key{name};
    if (const auto iter = ids_.find(key); iter != ids_.end()) {
        return iter->second;
    }

    const RelationType id{static_cast<std::uint32_t>(names_.size() + 1)};
    names_.push_back(key);
    ids_.emplace(key, id);
    return id;
}

std::optional<RelationType> RelationRegistry::find(std::string_view name) const {
    const auto iter = ids_.find(std::string{name});
    if (iter == ids_.end()) {
        return std::nullopt;
    }
    return iter->second;
}

const std::string& RelationRegistry::name(RelationType type) const {
    if (!type.valid() || type.id > names_.size()) {
        throw std::out_of_range(fmt::format("unknown relation {}", to_string(type)));
    }
    return names_[type.id - 1];
}

std::size_t RelationRegistry::size() const noexcept {
    return names_.size();
}

const std::vector<std::string>& RelationRegistry::names() const noexcept {
    return names_;
}

std::string to_string(RelationType relation) {
    return relation.valid() ? fmt::format("R{}", relation.id) : "R<invalid>";
}

std::string to_string(CausalRole role) {
    switch (role) {
    case CausalRole::Structural:
        return "Structural";
    case CausalRole::Observational:
        return "Observational";
    case CausalRole::Generative:
        return "Generative";
    case CausalRole::Inhibitory:
        return "Inhibitory";
    case CausalRole::Transformative:
        return "Transformative";
    case CausalRole::Ancestral:
        return "Ancestral";
    case CausalRole::Symbolic:
        return "Symbolic";
    }
    return "Structural";
}

CausalRole causal_role_from_string(std::string_view value) {
    const auto role = normalized(value);
    if (role == "observational") {
        return CausalRole::Observational;
    }
    if (role == "generative") {
        return CausalRole::Generative;
    }
    if (role == "inhibitory") {
        return CausalRole::Inhibitory;
    }
    if (role == "transformative") {
        return CausalRole::Transformative;
    }
    if (role == "ancestral") {
        return CausalRole::Ancestral;
    }
    if (role == "symbolic") {
        return CausalRole::Symbolic;
    }
    return CausalRole::Structural;
}

}  // namespace pv
