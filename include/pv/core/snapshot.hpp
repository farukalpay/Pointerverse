// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "pv/core/attribute.hpp"
#include "pv/core/fact.hpp"
#include "pv/hash/canonical.hpp"
#include "pv/core/object.hpp"
#include "pv/core/pointer.hpp"

namespace pv {

struct ObjectSnapshot {
    ObjectId id;
    std::string name;
    TypeId type;
    ExistenceState existence{ExistenceState::Alive};
    std::vector<Attribute> attributes;
    std::size_t incoming_count{0};
    std::size_t outgoing_count{0};
};

struct PointerSnapshot {
    PointerId id;
    ObjectId from;
    ObjectId to;
    RelationType relation;
    CausalRole causal_role{CausalRole::Structural};
    Weight weight;
    Epoch born_at;
    std::optional<Epoch> expires_at;
    std::string law_domain{"core"};
    std::vector<Attribute> attributes;
};

struct WorldSnapshot {
    WorldId world;
    std::string world_name;
    Epoch epoch;
    std::vector<ObjectSnapshot> objects;
    std::vector<PointerSnapshot> pointers;
    std::vector<Fact> facts;
    std::map<std::uint32_t, std::string> type_names;
    std::map<std::uint32_t, std::string> relation_names;

    [[nodiscard]] bool contains(ObjectId id) const noexcept;
    [[nodiscard]] const ObjectSnapshot* object(ObjectId id) const noexcept;
    [[nodiscard]] const PointerSnapshot* pointer(PointerId id) const noexcept;
    [[nodiscard]] std::string type_name(TypeId type) const;
    [[nodiscard]] std::string relation_name(RelationType relation) const;
    [[nodiscard]] Hash256 canonical_hash() const;
    [[nodiscard]] std::uint64_t structural_hash() const;
};

}  // namespace pv
