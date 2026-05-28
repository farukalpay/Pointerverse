// SPDX-License-Identifier: Apache-2.0
#include "pv/core/operation.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <optional>

#include "pv/core/delta.hpp"

namespace pv {
namespace {

OperationId next_operation_id(const std::vector<Operation>& ops) noexcept {
    std::uint64_t next = 1;
    for (const auto& op : ops) {
        next = std::max(next, op.id.value + 1);
    }
    return OperationId{next};
}

}  // namespace

Operation make_operation(OperationKind kind, OperationBody body, OperationId id) {
    return Operation{id, kind, std::move(body)};
}

std::string to_string(TempObjectId id) {
    return id.valid() ? fmt::format("T{}", id.value) : "T<invalid>";
}

std::string to_string(OperationId id) {
    return id.valid() ? fmt::format("O{}", id.value) : "O<invalid>";
}

std::string to_string(const ObjectRef& ref) {
    return std::visit(
        [](const auto& value) {
            return to_string(value);
        },
        ref);
}

std::string to_string(OperationKind kind) {
    switch (kind) {
    case OperationKind::CreateObject:
        return "CreateObject";
    case OperationKind::SetObjectType:
        return "SetObjectType";
    case OperationKind::SetObjectExistence:
        return "SetObjectExistence";
    case OperationKind::SetObjectAttribute:
        return "SetObjectAttribute";
    case OperationKind::RemoveObjectAttribute:
        return "RemoveObjectAttribute";
    case OperationKind::CreatePointer:
        return "CreatePointer";
    case OperationKind::ExpirePointer:
        return "ExpirePointer";
    case OperationKind::SetPointerWeight:
        return "SetPointerWeight";
    case OperationKind::SetPointerAttribute:
        return "SetPointerAttribute";
    case OperationKind::RemovePointerAttribute:
        return "RemovePointerAttribute";
    case OperationKind::EmitEvent:
        return "EmitEvent";
    case OperationKind::InternType:
        return "InternType";
    case OperationKind::InternRelation:
        return "InternRelation";
    case OperationKind::AssertObject:
        return "AssertObject";
    case OperationKind::AssertPointer:
        return "AssertPointer";
    case OperationKind::AssertFact:
        return "AssertFact";
    }
    return "EmitEvent";
}

bool Delta::empty() const noexcept {
    return ops.empty();
}

void Delta::append(Operation operation) {
    if (!operation.id.valid()) {
        operation.id = next_operation_id(ops);
    }
    ops.push_back(std::move(operation));
}

void Delta::append_create(ObjectCreate create) {
    append(make_operation(OperationKind::CreateObject, std::move(create)));
}

void Delta::append_update(ObjectUpdate update) {
    if (update.type.has_value()) {
        append(make_operation(OperationKind::SetObjectType, SetObjectTypeOp{update.object, *update.type}));
    }
    if (update.existence.has_value()) {
        append(make_operation(OperationKind::SetObjectExistence, SetObjectExistenceOp{update.object, *update.existence}));
    }
}

void Delta::append_link(PointerCreate link) {
    append(make_operation(OperationKind::CreatePointer, std::move(link)));
}

void Delta::append_unlink(PointerRemove unlink) {
    append(make_operation(OperationKind::ExpirePointer, unlink));
}

void Delta::append_event(TraceEvent event) {
    append(make_operation(OperationKind::EmitEvent, EmitEventOp{std::move(event)}));
}

void Delta::append_set_object_attribute(ObjectRef object, Attribute attribute) {
    append(make_operation(OperationKind::SetObjectAttribute, SetObjectAttributeOp{std::move(object), std::move(attribute)}));
}

void Delta::append_remove_object_attribute(ObjectRef object, std::string key) {
    append(make_operation(OperationKind::RemoveObjectAttribute, RemoveObjectAttributeOp{std::move(object), std::move(key)}));
}

void Delta::append_set_pointer_weight(PointerId pointer, Weight weight) {
    append(make_operation(OperationKind::SetPointerWeight, SetPointerWeightOp{pointer, weight}));
}

void Delta::append_set_pointer_attribute(PointerId pointer, Attribute attribute) {
    append(make_operation(OperationKind::SetPointerAttribute, SetPointerAttributeOp{pointer, std::move(attribute)}));
}

void Delta::append_remove_pointer_attribute(PointerId pointer, std::string key) {
    append(make_operation(OperationKind::RemovePointerAttribute, RemovePointerAttributeOp{pointer, std::move(key)}));
}

void Delta::append_intern_type(std::string name, TypeId id) {
    append(make_operation(OperationKind::InternType, InternTypeOp{std::move(name), id}));
}

void Delta::append_intern_relation(std::string name, RelationType id) {
    append(make_operation(OperationKind::InternRelation, InternRelationOp{std::move(name), id}));
}

void Delta::append_assert_object(ObjectRef object) {
    append(make_operation(OperationKind::AssertObject, AssertObjectOp{std::move(object)}));
}

void Delta::append_assert_pointer(PointerId pointer) {
    append(make_operation(OperationKind::AssertPointer, AssertPointerOp{pointer}));
}

void Delta::append_assert_fact(FactId fact) {
    append(make_operation(OperationKind::AssertFact, AssertFactOp{fact}));
}

std::vector<ObjectCreate> Delta::creates_view() const {
    std::vector<ObjectCreate> out;
    for (const auto& op : ops) {
        if (op.kind == OperationKind::CreateObject) {
            out.push_back(std::get<CreateObjectOp>(op.body));
        }
    }
    return out;
}

std::vector<ObjectUpdate> Delta::updates_view() const {
    std::vector<ObjectUpdate> out;
    for (const auto& op : ops) {
        if (op.kind == OperationKind::SetObjectType) {
            const auto& body = std::get<SetObjectTypeOp>(op.body);
            out.push_back(ObjectUpdate{body.object, body.type, std::nullopt});
        } else if (op.kind == OperationKind::SetObjectExistence) {
            const auto& body = std::get<SetObjectExistenceOp>(op.body);
            out.push_back(ObjectUpdate{body.object, std::nullopt, body.existence});
        }
    }
    return out;
}

std::vector<PointerCreate> Delta::links_view() const {
    std::vector<PointerCreate> out;
    for (const auto& op : ops) {
        if (op.kind == OperationKind::CreatePointer) {
            out.push_back(std::get<CreatePointerOp>(op.body));
        }
    }
    return out;
}

std::vector<PointerRemove> Delta::unlinks_view() const {
    std::vector<PointerRemove> out;
    for (const auto& op : ops) {
        if (op.kind == OperationKind::ExpirePointer) {
            out.push_back(std::get<ExpirePointerOp>(op.body));
        }
    }
    return out;
}

std::vector<TraceEvent> Delta::events_view() const {
    std::vector<TraceEvent> out;
    for (const auto& op : ops) {
        if (op.kind == OperationKind::EmitEvent) {
            out.push_back(std::get<EmitEventOp>(op.body).event);
        }
    }
    return out;
}

}  // namespace pv
