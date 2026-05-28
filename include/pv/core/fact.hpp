// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "pv/core/attribute.hpp"
#include "pv/core/object.hpp"
#include "pv/core/pointer.hpp"
#include "pv/hash/canonical.hpp"

namespace pv {

struct WorldSnapshot;

enum class FactKind : std::uint8_t {
    ObjectFact,
    PointerFact,
    AttributeFact,
    EvidenceFact,
    LawFact
};

struct FactId {
    Hash256 value;

    friend bool operator==(const FactId&, const FactId&) = default;
};

struct ObjectFactPayload {
    ObjectId object;
    std::string name;
    TypeId type;
    ExistenceState existence{ExistenceState::Alive};
};

struct PointerFactPayload {
    PointerId pointer;
    ObjectId from;
    ObjectId to;
    RelationType relation;
    CausalRole causal_role{CausalRole::Structural};
    Weight weight;
    Epoch born_at;
    std::optional<Epoch> expires_at;
    std::string law_domain{"core"};
};

struct ObjectAttributeSubject {
    ObjectId object;
};

struct PointerAttributeSubject {
    PointerId pointer;
};

using AttributeSubject = std::variant<ObjectAttributeSubject, PointerAttributeSubject>;

struct AttributeFactPayload {
    AttributeSubject subject;
    std::string key;
    Value value;
};

struct EvidenceFactPayload {
    Hash256 evidence;
    std::string label;
};

struct LawFactPayload {
    std::string law;
    bool passed{true};
    Hash256 input_hash;
    Hash256 output_hash;
};

using FactPayload = std::variant<
    ObjectFactPayload,
    PointerFactPayload,
    AttributeFactPayload,
    EvidenceFactPayload,
    LawFactPayload>;

struct Fact {
    FactId id;
    FactKind kind{FactKind::ObjectFact};
    Epoch born_at;
    std::optional<Epoch> expired_at;
    Hash256 payload_hash;
    FactPayload payload;
};

[[nodiscard]] bool operator<(const FactId& left, const FactId& right) noexcept;
[[nodiscard]] bool operator<(const Fact& left, const Fact& right) noexcept;
[[nodiscard]] Fact make_fact(FactKind kind, Epoch born_at, std::optional<Epoch> expired_at, FactPayload payload);
[[nodiscard]] std::vector<Fact> derive_facts(const WorldSnapshot& snapshot);
[[nodiscard]] std::string to_string(FactKind kind);
[[nodiscard]] std::string to_string(FactId id);

}  // namespace pv
