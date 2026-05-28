// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

#include "pv/core/attribute.hpp"
#include "pv/core/id.hpp"
#include "pv/core/relation.hpp"

namespace pv {

struct PointerId {
    std::uint64_t value{0};

    [[nodiscard]] bool valid() const noexcept { return value != 0; }

    friend bool operator==(PointerId, PointerId) = default;
};

struct Weight {
    double value{1.0};
};

struct PointerEdge {
    PointerId id;
    ObjectId from;
    ObjectId to;
    RelationType relation;
    CausalRole causal_role{CausalRole::Structural};
    Weight weight;
    Epoch born_at;
    std::optional<Epoch> expires_at;
    std::string law_domain{"core"};
    std::unordered_map<std::string, Value> attributes;

    [[nodiscard]] bool active_at(Epoch epoch) const noexcept;
};

[[nodiscard]] std::string to_string(PointerId id);

}  // namespace pv
