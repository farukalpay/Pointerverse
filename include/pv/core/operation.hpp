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
#include "pv/trace/event.hpp"

namespace pv {

struct TempObjectId {
    std::uint32_t value{0};

    [[nodiscard]] bool valid() const noexcept { return value != 0; }

    friend bool operator==(TempObjectId, TempObjectId) = default;
};

using ObjectRef = std::variant<ObjectId, TempObjectId>;

struct OperationId {
    std::uint64_t value{0};

    [[nodiscard]] bool valid() const noexcept { return value != 0; }

    friend bool operator==(OperationId, OperationId) = default;
};

enum class OperationKind : std::uint8_t {
    CreateObject,
    SetObjectType,
    SetObjectExistence,
    SetObjectAttribute,
    RemoveObjectAttribute,

    CreatePointer,
    ExpirePointer,
    SetPointerWeight,
    SetPointerAttribute,
    RemovePointerAttribute,

    EmitEvent
};

struct CreateObjectOp {
    TempObjectId temp_id;
    std::string name;
    TypeId type;
    ExistenceState existence{ExistenceState::Alive};
    std::vector<Attribute> attributes;
};

struct SetObjectTypeOp {
    ObjectRef object;
    TypeId type;
};

struct SetObjectExistenceOp {
    ObjectRef object;
    ExistenceState existence{ExistenceState::Alive};
};

struct SetObjectAttributeOp {
    ObjectRef object;
    Attribute attribute;
};

struct RemoveObjectAttributeOp {
    ObjectRef object;
    std::string key;
};

struct CreatePointerOp {
    ObjectRef from;
    ObjectRef to;
    RelationType relation;
    CausalRole causal_role{CausalRole::Structural};
    Weight weight;
    std::string law_domain{"core"};
    std::vector<Attribute> attributes;
};

struct ExpirePointerOp {
    PointerId id;
};

struct SetPointerWeightOp {
    PointerId id;
    Weight weight;
};

struct SetPointerAttributeOp {
    PointerId id;
    Attribute attribute;
};

struct RemovePointerAttributeOp {
    PointerId id;
    std::string key;
};

struct EmitEventOp {
    TraceEvent event;
};

using OperationBody = std::variant<
    CreateObjectOp,
    SetObjectTypeOp,
    SetObjectExistenceOp,
    SetObjectAttributeOp,
    RemoveObjectAttributeOp,
    CreatePointerOp,
    ExpirePointerOp,
    SetPointerWeightOp,
    SetPointerAttributeOp,
    RemovePointerAttributeOp,
    EmitEventOp>;

struct Operation {
    OperationId id;
    OperationKind kind{OperationKind::EmitEvent};
    OperationBody body;
};

using ObjectCreate = CreateObjectOp;
using PointerCreate = CreatePointerOp;
using PointerRemove = ExpirePointerOp;

struct ObjectUpdate {
    ObjectRef object;
    std::optional<TypeId> type;
    std::optional<ExistenceState> existence;
};

[[nodiscard]] Operation make_operation(OperationKind kind, OperationBody body, OperationId id = {});
[[nodiscard]] std::string to_string(TempObjectId id);
[[nodiscard]] std::string to_string(OperationId id);
[[nodiscard]] std::string to_string(const ObjectRef& ref);
[[nodiscard]] std::string to_string(OperationKind kind);

}  // namespace pv
