// SPDX-License-Identifier: Apache-2.0
#include "pv/compiler/program_builder.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace pv {
namespace {

Value u64(std::uint64_t value) {
    return uint64_value(value);
}

Value f64(double value) {
    return float64_value(value);
}

const ObjectSnapshot* object_by_name(const WorldSnapshot& snapshot, std::string_view name) {
    for (const auto& object : snapshot.objects) {
        if (object.name == name) {
            return &object;
        }
    }
    return nullptr;
}

}  // namespace

void ProgramBuilder::import_symbols(const WorldSnapshot& snapshot) {
    for (const auto& [id, name] : snapshot.type_names) {
        if (id == 0 || name.empty() || type_symbols_.contains(name)) {
            continue;
        }
        if (program_.symbols.type_names.size() < id) {
            program_.symbols.type_names.resize(id);
        }
        program_.symbols.type_names[id - 1] = name;
        type_symbols_.emplace(name, static_cast<std::uint64_t>(id - 1));
    }
    for (const auto& [id, name] : snapshot.relation_names) {
        if (id == 0 || name.empty() || relation_symbols_.contains(name)) {
            continue;
        }
        if (program_.symbols.relation_names.size() < id) {
            program_.symbols.relation_names.resize(id);
        }
        program_.symbols.relation_names[id - 1] = name;
        relation_symbols_.emplace(name, static_cast<std::uint64_t>(id - 1));
    }
}

std::uint64_t ProgramBuilder::string_symbol(std::string_view value) {
    const std::string key{value};
    if (const auto iter = string_symbols_.find(key); iter != string_symbols_.end()) {
        return iter->second;
    }
    const auto index = static_cast<std::uint64_t>(program_.symbols.strings.size());
    program_.symbols.strings.push_back(key);
    string_symbols_.emplace(key, index);
    return index;
}

std::uint64_t ProgramBuilder::type_symbol(std::string_view value) {
    const std::string key{value};
    if (const auto iter = type_symbols_.find(key); iter != type_symbols_.end()) {
        return iter->second;
    }
    const auto index = static_cast<std::uint64_t>(program_.symbols.type_names.size());
    program_.symbols.type_names.push_back(key);
    type_symbols_.emplace(key, index);
    return index;
}

std::uint64_t ProgramBuilder::relation_symbol(std::string_view value) {
    const std::string key{value};
    if (const auto iter = relation_symbols_.find(key); iter != relation_symbols_.end()) {
        return iter->second;
    }
    const auto index = static_cast<std::uint64_t>(program_.symbols.relation_names.size());
    program_.symbols.relation_names.push_back(key);
    relation_symbols_.emplace(key, index);
    return index;
}

void ProgramBuilder::intern_type(std::string_view name) {
    emit(InstructionOp::InternType, {u64(type_symbol(name))});
}

void ProgramBuilder::intern_relation(std::string_view name) {
    emit(InstructionOp::InternRelation, {u64(relation_symbol(name))});
}

ProgramObjectRef ProgramBuilder::object_by_name(std::string_view name) {
    return ProgramObjectRef{1, string_symbol(name)};
}

ProgramObjectRef ProgramBuilder::temp_object(std::uint32_t temp) {
    return ProgramObjectRef{0, temp};
}

ProgramPointerRef ProgramBuilder::pointer_by_id(PointerId id) {
    return ProgramPointerRef{1, id.value};
}

ProgramPointerRef ProgramBuilder::temp_pointer(std::uint64_t temp) {
    return ProgramPointerRef{0, temp};
}

ProgramObjectRef ProgramBuilder::create_object(std::string_view name, std::string_view type) {
    intern_type(type);
    const auto temp = next_temp_object_++;
    auto ref = temp_object(temp);
    temp_objects_.emplace(temp, ref);
    created_names_.emplace(std::string{name}, ref);
    emit(InstructionOp::CreateObject, {u64(temp), u64(string_symbol(name)), u64(type_symbol(type))});
    return ref;
}

ProgramObjectRef ProgramBuilder::ensure_object(const WorldSnapshot& snapshot, std::string_view name, std::string_view type) {
    if (const auto iter = created_names_.find(std::string{name}); iter != created_names_.end()) {
        return iter->second;
    }
    if (::pv::object_by_name(snapshot, name) != nullptr) {
        auto ref = object_by_name(name);
        assert_object(ref);
        return ref;
    }
    return create_object(name, type);
}

ProgramPointerRef ProgramBuilder::create_pointer(
    ProgramObjectRef from,
    ProgramObjectRef to,
    std::string_view relation,
    double weight,
    CausalRole role,
    std::string_view law_domain) {
    intern_relation(relation);
    const auto temp = next_temp_pointer_++;
    emit(
        InstructionOp::CreatePointer,
        {
            u64(temp),
            u64(from.tag),
            u64(from.value),
            u64(to.tag),
            u64(to.value),
            u64(relation_symbol(relation)),
            f64(weight),
            u64(static_cast<std::uint64_t>(role)),
            u64(string_symbol(law_domain))
        });
    return temp_pointer(temp);
}

void ProgramBuilder::set_object_type(ProgramObjectRef object, std::string_view type) {
    intern_type(type);
    emit(InstructionOp::SetObjectType, {u64(object.tag), u64(object.value), u64(type_symbol(type))});
}

void ProgramBuilder::set_object_existence(ProgramObjectRef object, ExistenceState existence) {
    emit(
        InstructionOp::SetObjectExistence,
        {u64(object.tag), u64(object.value), u64(static_cast<std::uint64_t>(existence))});
}

void ProgramBuilder::set_object_attribute(ProgramObjectRef object, std::string_view key, Value value) {
    emit(InstructionOp::SetObjectAttribute, {u64(object.tag), u64(object.value), u64(string_symbol(key)), std::move(value)});
}

void ProgramBuilder::remove_object_attribute(ProgramObjectRef object, std::string_view key) {
    emit(InstructionOp::RemoveObjectAttribute, {u64(object.tag), u64(object.value), u64(string_symbol(key))});
}

void ProgramBuilder::set_pointer_weight(ProgramPointerRef pointer, double weight) {
    emit(InstructionOp::SetPointerWeight, {u64(pointer.tag), u64(pointer.value), f64(weight)});
}

void ProgramBuilder::set_pointer_attribute(ProgramPointerRef pointer, std::string_view key, Value value) {
    emit(InstructionOp::SetPointerAttribute, {u64(pointer.tag), u64(pointer.value), u64(string_symbol(key)), std::move(value)});
}

void ProgramBuilder::remove_pointer_attribute(ProgramPointerRef pointer, std::string_view key) {
    emit(InstructionOp::RemovePointerAttribute, {u64(pointer.tag), u64(pointer.value), u64(string_symbol(key))});
}

void ProgramBuilder::expire_pointer(ProgramPointerRef pointer) {
    emit(InstructionOp::ExpirePointer, {u64(pointer.tag), u64(pointer.value)});
}

void ProgramBuilder::assert_object(ProgramObjectRef object) {
    emit(InstructionOp::AssertObject, {u64(object.tag), u64(object.value)});
}

void ProgramBuilder::assert_pointer(ProgramPointerRef pointer) {
    emit(InstructionOp::AssertPointer, {u64(pointer.tag), u64(pointer.value)});
}

void ProgramBuilder::assert_fact(FactId fact) {
    emit(InstructionOp::AssertFact, {hash_value(fact.value)});
}

void ProgramBuilder::emit_event(const TraceEvent& event) {
    std::vector<Value> args;
    args.push_back(u64(string_symbol(event.event)));
    args.push_back(u64(event.fields.size()));
    for (const auto& [key, value] : event.fields) {
        args.push_back(u64(string_symbol(key)));
        args.push_back(string_value(value));
    }
    args.push_back(u64(event.measurements.size()));
    for (const auto& [key, value] : event.measurements) {
        args.push_back(u64(string_symbol(key)));
        args.push_back(f64(value));
    }
    emit(InstructionOp::EmitEvent, std::move(args));
}

void ProgramBuilder::append_delta(const WorldSnapshot& snapshot, const Delta& delta) {
    for (const auto& op : delta.ops) {
        switch (op.kind) {
        case OperationKind::InternType:
            intern_type(std::get<InternTypeOp>(op.body).name);
            break;
        case OperationKind::InternRelation:
            intern_relation(std::get<InternRelationOp>(op.body).name);
            break;
        case OperationKind::CreateObject: {
            const auto& body = std::get<CreateObjectOp>(op.body);
            intern_type(type_name(snapshot, body.type));
            auto ref = temp_object(body.temp_id.value);
            temp_objects_.emplace(body.temp_id.value, ref);
            created_names_.emplace(body.name, ref);
            emit(
                InstructionOp::CreateObject,
                {u64(body.temp_id.value), u64(string_symbol(body.name)), u64(type_symbol(type_name(snapshot, body.type)))});
            next_temp_object_ = std::max(next_temp_object_, body.temp_id.value + 1);
            for (const auto& attribute : body.attributes) {
                set_object_attribute(ref, attribute.key, attribute.value);
            }
            break;
        }
        case OperationKind::SetObjectType: {
            const auto& body = std::get<SetObjectTypeOp>(op.body);
            set_object_type(ref_for(snapshot, body.object), type_name(snapshot, body.type));
            break;
        }
        case OperationKind::SetObjectExistence: {
            const auto& body = std::get<SetObjectExistenceOp>(op.body);
            set_object_existence(ref_for(snapshot, body.object), body.existence);
            break;
        }
        case OperationKind::SetObjectAttribute: {
            const auto& body = std::get<SetObjectAttributeOp>(op.body);
            set_object_attribute(ref_for(snapshot, body.object), body.attribute.key, body.attribute.value);
            break;
        }
        case OperationKind::RemoveObjectAttribute: {
            const auto& body = std::get<RemoveObjectAttributeOp>(op.body);
            remove_object_attribute(ref_for(snapshot, body.object), body.key);
            break;
        }
        case OperationKind::CreatePointer: {
            const auto& body = std::get<CreatePointerOp>(op.body);
            auto pointer = create_pointer(
                ref_for(snapshot, body.from),
                ref_for(snapshot, body.to),
                relation_name(snapshot, body.relation),
                body.weight.value,
                body.causal_role,
                body.law_domain);
            for (const auto& attribute : body.attributes) {
                set_pointer_attribute(pointer, attribute.key, attribute.value);
            }
            break;
        }
        case OperationKind::ExpirePointer:
            expire_pointer(pointer_by_id(std::get<ExpirePointerOp>(op.body).id));
            break;
        case OperationKind::SetPointerWeight: {
            const auto& body = std::get<SetPointerWeightOp>(op.body);
            set_pointer_weight(pointer_by_id(body.id), body.weight.value);
            break;
        }
        case OperationKind::SetPointerAttribute: {
            const auto& body = std::get<SetPointerAttributeOp>(op.body);
            set_pointer_attribute(pointer_by_id(body.id), body.attribute.key, body.attribute.value);
            break;
        }
        case OperationKind::RemovePointerAttribute: {
            const auto& body = std::get<RemovePointerAttributeOp>(op.body);
            remove_pointer_attribute(pointer_by_id(body.id), body.key);
            break;
        }
        case OperationKind::EmitEvent:
            emit_event(std::get<EmitEventOp>(op.body).event);
            break;
        case OperationKind::AssertObject:
            assert_object(ref_for(snapshot, std::get<AssertObjectOp>(op.body).object));
            break;
        case OperationKind::AssertPointer:
            assert_pointer(pointer_by_id(std::get<AssertPointerOp>(op.body).id));
            break;
        case OperationKind::AssertFact:
            assert_fact(std::get<AssertFactOp>(op.body).id);
            break;
        }
    }
}

Program ProgramBuilder::build() const {
    return program_;
}

void ProgramBuilder::emit(InstructionOp op, std::vector<Value> args) {
    program_.instructions.push_back(Instruction{op, std::move(args)});
}

ProgramObjectRef ProgramBuilder::ref_for(const WorldSnapshot& snapshot, const ObjectRef& ref) {
    if (const auto* id = std::get_if<ObjectId>(&ref)) {
        const auto* object = snapshot.object(*id);
        if (object == nullptr) {
            throw std::invalid_argument(fmt::format("cannot compile unknown object {}", to_string(*id)));
        }
        const auto iter = string_symbols_.find(object->name);
        if (iter != string_symbols_.end()) {
            return ProgramObjectRef{1, iter->second};
        }
        return ProgramObjectRef{1, string_symbol(object->name)};
    }
    const auto temp = std::get<TempObjectId>(ref);
    return ProgramObjectRef{0, temp.value};
}

std::string ProgramBuilder::type_name(const WorldSnapshot& snapshot, TypeId type) const {
    return snapshot.type_name(type);
}

std::string ProgramBuilder::relation_name(const WorldSnapshot& snapshot, RelationType relation) const {
    return snapshot.relation_name(relation);
}

}  // namespace pv
