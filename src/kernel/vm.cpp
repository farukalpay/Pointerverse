// SPDX-License-Identifier: Apache-2.0
#include "pv/kernel/vm.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <limits>
#include <map>
#include <sstream>
#include <unordered_map>
#include <utility>

#include "pv/kernel/executor.hpp"

namespace pv {
namespace {

constexpr std::uint64_t object_ref_temp = 0;
constexpr std::uint64_t object_ref_name = 1;
constexpr std::uint64_t pointer_ref_temp = 0;
constexpr std::uint64_t pointer_ref_id = 1;

std::uint32_t next_type_value(const WorldSnapshot& snapshot) noexcept {
    std::uint32_t next = 1;
    for (const auto& [id, _] : snapshot.type_names) {
        next = std::max(next, id + 1);
    }
    return next;
}

std::uint32_t next_relation_value(const WorldSnapshot& snapshot) noexcept {
    std::uint32_t next = 1;
    for (const auto& [id, _] : snapshot.relation_names) {
        next = std::max(next, id + 1);
    }
    return next;
}

std::uint64_t next_pointer_value(const WorldSnapshot& snapshot) noexcept {
    std::uint64_t next = 1;
    for (const auto& pointer : snapshot.pointers) {
        next = std::max(next, pointer.id.value + 1);
    }
    return next;
}

std::optional<ObjectId> object_named(const WorldSnapshot& snapshot, std::string_view name) {
    for (const auto& object : snapshot.objects) {
        if (object.name == name) {
            return object.id;
        }
    }
    return std::nullopt;
}

std::optional<TypeId> type_named(const WorldSnapshot& snapshot, std::string_view name) {
    for (const auto& [id, candidate] : snapshot.type_names) {
        if (candidate == name) {
            return TypeId{id};
        }
    }
    return std::nullopt;
}

std::optional<RelationType> relation_named(const WorldSnapshot& snapshot, std::string_view name) {
    for (const auto& [id, candidate] : snapshot.relation_names) {
        if (candidate == name) {
            return RelationType{id};
        }
    }
    return std::nullopt;
}

bool contains_pointer(const WorldSnapshot& snapshot, PointerId id) {
    return snapshot.pointer(id) != nullptr;
}

bool contains_fact(const WorldSnapshot& snapshot, FactId id) {
    auto facts = snapshot.facts.empty() ? derive_facts(snapshot) : snapshot.facts;
    return std::ranges::any_of(facts, [&](const Fact& fact) {
        return fact.id == id;
    });
}

bool has_object_ref_arg(const Instruction& instruction) {
    return std::ranges::any_of(instruction.args, [](const Value& value) {
        return value.kind == ValueKind::ObjectRef;
    });
}

std::optional<std::uint64_t> as_u64(const Value& value) {
    if (value.kind == ValueKind::UInt64) {
        return std::get<std::uint64_t>(value.data);
    }
    if (value.kind == ValueKind::Int64) {
        const auto signed_value = std::get<std::int64_t>(value.data);
        if (signed_value >= 0) {
            return static_cast<std::uint64_t>(signed_value);
        }
    }
    return std::nullopt;
}

std::optional<double> as_f64(const Value& value) {
    if (value.kind == ValueKind::Float64) {
        return std::get<double>(value.data);
    }
    if (value.kind == ValueKind::UInt64) {
        return static_cast<double>(std::get<std::uint64_t>(value.data));
    }
    if (value.kind == ValueKind::Int64) {
        return static_cast<double>(std::get<std::int64_t>(value.data));
    }
    return std::nullopt;
}

std::optional<Hash256> as_hash(const Value& value) {
    if (value.kind != ValueKind::Hash) {
        return std::nullopt;
    }
    return std::get<Hash256>(value.data);
}

struct VmFrame {
    VmFrame(const WorldSnapshot& before_snapshot, const Program& vm_program)
        : before(before_snapshot),
          program(vm_program),
          next_type(next_type_value(before_snapshot)),
          next_relation(next_relation_value(before_snapshot)),
          next_pointer(next_pointer_value(before_snapshot)) {}

    const WorldSnapshot& before;
    const Program& program;
    Delta delta;
    std::vector<VmDiagnostic> diagnostics;
    std::unordered_map<std::uint64_t, TypeId> type_symbols;
    std::unordered_map<std::uint64_t, RelationType> relation_symbols;
    std::unordered_map<std::uint64_t, TempObjectId> temp_objects;
    std::unordered_map<std::string, TempObjectId> created_names;
    std::unordered_map<std::uint64_t, PointerId> temp_pointers;
    std::uint32_t next_type{1};
    std::uint32_t next_relation{1};
    std::uint64_t next_pointer{1};
};

void add_diag(
    VmFrame& frame,
    std::size_t instruction,
    std::string code,
    std::string message,
    DiagnosticSeverity severity = DiagnosticSeverity::Error) {
    frame.diagnostics.push_back(VmDiagnostic{severity, instruction, std::move(code), std::move(message)});
}

bool arg_count(VmFrame& frame, std::size_t index, const Instruction& instruction, std::size_t expected) {
    if (instruction.args.size() == expected) {
        return true;
    }
    add_diag(
        frame,
        index,
        "E_INVALID_ARITY",
        fmt::format("{} expects {} argument(s), got {}", to_string(instruction.op), expected, instruction.args.size()));
    return false;
}

bool reject_raw_object_refs(VmFrame& frame, std::size_t index, const Instruction& instruction) {
    if (!has_object_ref_arg(instruction)) {
        return false;
    }
    add_diag(frame, index, "E_RAW_OBJECT_REF", "program operands cannot contain branch-local ObjectId values");
    return true;
}

std::optional<std::uint64_t> u64_arg(VmFrame& frame, std::size_t index, const Instruction& instruction, std::size_t arg) {
    if (arg >= instruction.args.size()) {
        return std::nullopt;
    }
    auto value = as_u64(instruction.args[arg]);
    if (!value.has_value()) {
        add_diag(
            frame,
            index,
            "E_INVALID_ARG",
            fmt::format("{} argument {} must be UInt64", to_string(instruction.op), arg));
    }
    return value;
}

std::optional<double> f64_arg(VmFrame& frame, std::size_t index, const Instruction& instruction, std::size_t arg) {
    if (arg >= instruction.args.size()) {
        return std::nullopt;
    }
    auto value = as_f64(instruction.args[arg]);
    if (!value.has_value()) {
        add_diag(
            frame,
            index,
            "E_INVALID_ARG",
            fmt::format("{} argument {} must be numeric", to_string(instruction.op), arg));
    }
    return value;
}

std::optional<std::string> string_symbol(
    VmFrame& frame,
    std::size_t index,
    const Instruction& instruction,
    std::size_t arg) {
    const auto symbol = u64_arg(frame, index, instruction, arg);
    if (!symbol.has_value()) {
        return std::nullopt;
    }
    if (*symbol >= frame.program.symbols.strings.size()) {
        add_diag(frame, index, "E_BAD_SYMBOL", fmt::format("string symbol {} is out of range", *symbol));
        return std::nullopt;
    }
    return frame.program.symbols.strings[static_cast<std::size_t>(*symbol)];
}

std::optional<std::string> type_symbol_name(
    VmFrame& frame,
    std::size_t index,
    const Instruction& instruction,
    std::size_t arg) {
    const auto symbol = u64_arg(frame, index, instruction, arg);
    if (!symbol.has_value()) {
        return std::nullopt;
    }
    if (*symbol >= frame.program.symbols.type_names.size()) {
        add_diag(frame, index, "E_BAD_SYMBOL", fmt::format("type symbol {} is out of range", *symbol));
        return std::nullopt;
    }
    return frame.program.symbols.type_names[static_cast<std::size_t>(*symbol)];
}

std::optional<std::string> relation_symbol_name(
    VmFrame& frame,
    std::size_t index,
    const Instruction& instruction,
    std::size_t arg) {
    const auto symbol = u64_arg(frame, index, instruction, arg);
    if (!symbol.has_value()) {
        return std::nullopt;
    }
    if (*symbol >= frame.program.symbols.relation_names.size()) {
        add_diag(frame, index, "E_BAD_SYMBOL", fmt::format("relation symbol {} is out of range", *symbol));
        return std::nullopt;
    }
    return frame.program.symbols.relation_names[static_cast<std::size_t>(*symbol)];
}

std::optional<TypeId> type_id_arg(VmFrame& frame, std::size_t index, const Instruction& instruction, std::size_t arg) {
    const auto symbol = u64_arg(frame, index, instruction, arg);
    if (!symbol.has_value()) {
        return std::nullopt;
    }
    const auto iter = frame.type_symbols.find(*symbol);
    if (iter == frame.type_symbols.end()) {
        add_diag(frame, index, "E_UNRESOLVED_TYPE", fmt::format("type symbol {} was used before InternType", *symbol));
        return std::nullopt;
    }
    return iter->second;
}

std::optional<RelationType> relation_id_arg(
    VmFrame& frame,
    std::size_t index,
    const Instruction& instruction,
    std::size_t arg) {
    const auto symbol = u64_arg(frame, index, instruction, arg);
    if (!symbol.has_value()) {
        return std::nullopt;
    }
    const auto iter = frame.relation_symbols.find(*symbol);
    if (iter == frame.relation_symbols.end()) {
        add_diag(
            frame,
            index,
            "E_UNRESOLVED_RELATION",
            fmt::format("relation symbol {} was used before InternRelation", *symbol));
        return std::nullopt;
    }
    return iter->second;
}

std::optional<ObjectRef> object_ref(
    VmFrame& frame,
    std::size_t index,
    const Instruction& instruction,
    std::size_t tag_arg,
    std::size_t value_arg) {
    const auto tag = u64_arg(frame, index, instruction, tag_arg);
    const auto value = u64_arg(frame, index, instruction, value_arg);
    if (!tag.has_value() || !value.has_value()) {
        return std::nullopt;
    }
    if (*tag == object_ref_temp) {
        const auto iter = frame.temp_objects.find(*value);
        if (iter == frame.temp_objects.end()) {
            add_diag(frame, index, "E_UNRESOLVED_OBJECT", fmt::format("temp object {} was not created", *value));
            return std::nullopt;
        }
        return ObjectRef{iter->second};
    }
    if (*tag == object_ref_name) {
        if (*value >= frame.program.symbols.strings.size()) {
            add_diag(frame, index, "E_BAD_SYMBOL", fmt::format("object name symbol {} is out of range", *value));
            return std::nullopt;
        }
        const auto& name = frame.program.symbols.strings[static_cast<std::size_t>(*value)];
        if (const auto created = frame.created_names.find(name); created != frame.created_names.end()) {
            return ObjectRef{created->second};
        }
        if (const auto id = object_named(frame.before, name); id.has_value()) {
            return ObjectRef{*id};
        }
        add_diag(
            frame,
            index,
            "E_UNRESOLVED_OBJECT",
            fmt::format("object \"{}\" was referenced before being created or imported", name));
        return std::nullopt;
    }
    add_diag(frame, index, "E_INVALID_REF", fmt::format("unknown object ref tag {}", *tag));
    return std::nullopt;
}

std::optional<PointerId> pointer_ref(
    VmFrame& frame,
    std::size_t index,
    const Instruction& instruction,
    std::size_t tag_arg,
    std::size_t value_arg) {
    const auto tag = u64_arg(frame, index, instruction, tag_arg);
    const auto value = u64_arg(frame, index, instruction, value_arg);
    if (!tag.has_value() || !value.has_value()) {
        return std::nullopt;
    }
    if (*tag == pointer_ref_temp) {
        const auto iter = frame.temp_pointers.find(*value);
        if (iter == frame.temp_pointers.end()) {
            add_diag(frame, index, "E_UNRESOLVED_POINTER", fmt::format("temp pointer {} was not created", *value));
            return std::nullopt;
        }
        return iter->second;
    }
    if (*tag == pointer_ref_id) {
        const auto pointer = PointerId{*value};
        if (!contains_pointer(frame.before, pointer)) {
            add_diag(frame, index, "E_UNRESOLVED_POINTER", fmt::format("pointer {} does not exist", to_string(pointer)));
            return std::nullopt;
        }
        return pointer;
    }
    add_diag(frame, index, "E_INVALID_REF", fmt::format("unknown pointer ref tag {}", *tag));
    return std::nullopt;
}

std::optional<Value> value_arg(VmFrame& frame, std::size_t index, const Instruction& instruction, std::size_t arg) {
    if (arg >= instruction.args.size()) {
        return std::nullopt;
    }
    if (instruction.args[arg].kind == ValueKind::ObjectRef) {
        add_diag(frame, index, "E_RAW_OBJECT_REF", "attribute values cannot contain branch-local ObjectId values");
        return std::nullopt;
    }
    return instruction.args[arg];
}

std::string value_to_field(const Value& value) {
    return to_string(value);
}

std::optional<double> value_to_measurement(const Value& value) {
    return as_f64(value);
}

bool execute_instruction(VmFrame& frame, std::size_t index, const Instruction& instruction) {
    if (reject_raw_object_refs(frame, index, instruction)) {
        return false;
    }

    switch (instruction.op) {
    case InstructionOp::InternType: {
        if (!arg_count(frame, index, instruction, 1)) {
            return false;
        }
        const auto symbol = u64_arg(frame, index, instruction, 0);
        const auto name = type_symbol_name(frame, index, instruction, 0);
        if (!symbol.has_value() || !name.has_value()) {
            return false;
        }
        auto id = type_named(frame.before, *name);
        if (!id.has_value()) {
            const auto desired = static_cast<std::uint32_t>(*symbol + 1);
            if (const auto iter = frame.before.type_names.find(desired);
                iter != frame.before.type_names.end() && iter->second != *name) {
                add_diag(
                    frame,
                    index,
                    "E_TYPE_ID_CONFLICT",
                    fmt::format("type symbol {} conflicts with existing type id {}", *symbol, desired));
                return false;
            }
            id = TypeId{desired};
            frame.next_type = std::max(frame.next_type, desired + 1);
        }
        frame.type_symbols[*symbol] = *id;
        frame.delta.append_intern_type(*name, *id);
        return true;
    }
    case InstructionOp::InternRelation: {
        if (!arg_count(frame, index, instruction, 1)) {
            return false;
        }
        const auto symbol = u64_arg(frame, index, instruction, 0);
        const auto name = relation_symbol_name(frame, index, instruction, 0);
        if (!symbol.has_value() || !name.has_value()) {
            return false;
        }
        auto id = relation_named(frame.before, *name);
        if (!id.has_value()) {
            const auto desired = static_cast<std::uint32_t>(*symbol + 1);
            if (const auto iter = frame.before.relation_names.find(desired);
                iter != frame.before.relation_names.end() && iter->second != *name) {
                add_diag(
                    frame,
                    index,
                    "E_RELATION_ID_CONFLICT",
                    fmt::format("relation symbol {} conflicts with existing relation id {}", *symbol, desired));
                return false;
            }
            id = RelationType{desired};
            frame.next_relation = std::max(frame.next_relation, desired + 1);
        }
        frame.relation_symbols[*symbol] = *id;
        frame.delta.append_intern_relation(*name, *id);
        return true;
    }
    case InstructionOp::CreateObject: {
        if (!arg_count(frame, index, instruction, 3)) {
            return false;
        }
        const auto temp = u64_arg(frame, index, instruction, 0);
        const auto name = string_symbol(frame, index, instruction, 1);
        const auto type = type_id_arg(frame, index, instruction, 2);
        if (!temp.has_value() || !name.has_value() || !type.has_value()) {
            return false;
        }
        if (*temp == 0 || *temp > std::numeric_limits<std::uint32_t>::max()) {
            add_diag(frame, index, "E_INVALID_TEMP", "object temp id must fit in uint32 and be non-zero");
            return false;
        }
        if (frame.temp_objects.contains(*temp) || frame.created_names.contains(*name) || object_named(frame.before, *name)) {
            add_diag(frame, index, "E_DUPLICATE_OBJECT", fmt::format("object \"{}\" already exists", *name));
            return false;
        }
        const auto temp_id = TempObjectId{static_cast<std::uint32_t>(*temp)};
        frame.temp_objects.emplace(*temp, temp_id);
        frame.created_names.emplace(*name, temp_id);
        frame.delta.append_create(ObjectCreate{temp_id, *name, *type, ExistenceState::Alive, {}});
        return true;
    }
    case InstructionOp::SetObjectType: {
        if (!arg_count(frame, index, instruction, 3)) {
            return false;
        }
        const auto object = object_ref(frame, index, instruction, 0, 1);
        const auto type = type_id_arg(frame, index, instruction, 2);
        if (!object.has_value() || !type.has_value()) {
            return false;
        }
        frame.delta.append_update(ObjectUpdate{*object, *type, std::nullopt});
        return true;
    }
    case InstructionOp::SetObjectExistence: {
        if (!arg_count(frame, index, instruction, 3)) {
            return false;
        }
        const auto object = object_ref(frame, index, instruction, 0, 1);
        const auto raw = u64_arg(frame, index, instruction, 2);
        if (!object.has_value() || !raw.has_value()) {
            return false;
        }
        if (*raw > static_cast<std::uint64_t>(ExistenceState::Tombstoned)) {
            add_diag(frame, index, "E_INVALID_ENUM", "existence state is out of range");
            return false;
        }
        frame.delta.append_update(ObjectUpdate{*object, std::nullopt, static_cast<ExistenceState>(*raw)});
        return true;
    }
    case InstructionOp::SetObjectAttribute: {
        if (!arg_count(frame, index, instruction, 4)) {
            return false;
        }
        const auto object = object_ref(frame, index, instruction, 0, 1);
        const auto key = string_symbol(frame, index, instruction, 2);
        const auto value = value_arg(frame, index, instruction, 3);
        if (!object.has_value() || !key.has_value() || !value.has_value()) {
            return false;
        }
        frame.delta.append_set_object_attribute(*object, Attribute{*key, *value});
        return true;
    }
    case InstructionOp::RemoveObjectAttribute: {
        if (!arg_count(frame, index, instruction, 3)) {
            return false;
        }
        const auto object = object_ref(frame, index, instruction, 0, 1);
        const auto key = string_symbol(frame, index, instruction, 2);
        if (!object.has_value() || !key.has_value()) {
            return false;
        }
        frame.delta.append_remove_object_attribute(*object, *key);
        return true;
    }
    case InstructionOp::CreatePointer: {
        if (!arg_count(frame, index, instruction, 9)) {
            return false;
        }
        const auto temp = u64_arg(frame, index, instruction, 0);
        const auto from = object_ref(frame, index, instruction, 1, 2);
        const auto to = object_ref(frame, index, instruction, 3, 4);
        const auto relation = relation_id_arg(frame, index, instruction, 5);
        const auto weight = f64_arg(frame, index, instruction, 6);
        const auto role_raw = u64_arg(frame, index, instruction, 7);
        const auto law_domain = string_symbol(frame, index, instruction, 8);
        if (!temp.has_value() || !from.has_value() || !to.has_value() || !relation.has_value()
            || !weight.has_value() || !role_raw.has_value() || !law_domain.has_value()) {
            return false;
        }
        if (*temp == 0) {
            add_diag(frame, index, "E_INVALID_TEMP", "pointer temp id must be non-zero");
            return false;
        }
        if (*role_raw > static_cast<std::uint64_t>(CausalRole::Symbolic)) {
            add_diag(frame, index, "E_INVALID_ENUM", "causal role is out of range");
            return false;
        }
        if (frame.temp_pointers.contains(*temp)) {
            add_diag(frame, index, "E_DUPLICATE_POINTER", fmt::format("temp pointer {} already exists", *temp));
            return false;
        }
        frame.temp_pointers.emplace(*temp, PointerId{frame.next_pointer++});
        frame.delta.append_link(PointerCreate{
            *from,
            *to,
            *relation,
            static_cast<CausalRole>(*role_raw),
            Weight{*weight},
            law_domain->empty() ? "core" : *law_domain,
            {}
        });
        return true;
    }
    case InstructionOp::SetPointerWeight: {
        if (!arg_count(frame, index, instruction, 3)) {
            return false;
        }
        const auto pointer = pointer_ref(frame, index, instruction, 0, 1);
        const auto weight = f64_arg(frame, index, instruction, 2);
        if (!pointer.has_value() || !weight.has_value()) {
            return false;
        }
        frame.delta.append_set_pointer_weight(*pointer, Weight{*weight});
        return true;
    }
    case InstructionOp::SetPointerAttribute: {
        if (!arg_count(frame, index, instruction, 4)) {
            return false;
        }
        const auto pointer = pointer_ref(frame, index, instruction, 0, 1);
        const auto key = string_symbol(frame, index, instruction, 2);
        const auto value = value_arg(frame, index, instruction, 3);
        if (!pointer.has_value() || !key.has_value() || !value.has_value()) {
            return false;
        }
        frame.delta.append_set_pointer_attribute(*pointer, Attribute{*key, *value});
        return true;
    }
    case InstructionOp::RemovePointerAttribute: {
        if (!arg_count(frame, index, instruction, 3)) {
            return false;
        }
        const auto pointer = pointer_ref(frame, index, instruction, 0, 1);
        const auto key = string_symbol(frame, index, instruction, 2);
        if (!pointer.has_value() || !key.has_value()) {
            return false;
        }
        frame.delta.append_remove_pointer_attribute(*pointer, *key);
        return true;
    }
    case InstructionOp::ExpirePointer: {
        if (!arg_count(frame, index, instruction, 2)) {
            return false;
        }
        const auto pointer = pointer_ref(frame, index, instruction, 0, 1);
        if (!pointer.has_value()) {
            return false;
        }
        frame.delta.append_unlink(PointerRemove{*pointer});
        return true;
    }
    case InstructionOp::AssertObject: {
        if (!arg_count(frame, index, instruction, 2)) {
            return false;
        }
        const auto object = object_ref(frame, index, instruction, 0, 1);
        if (!object.has_value()) {
            return false;
        }
        frame.delta.append_assert_object(*object);
        return true;
    }
    case InstructionOp::AssertPointer: {
        if (!arg_count(frame, index, instruction, 2)) {
            return false;
        }
        const auto pointer = pointer_ref(frame, index, instruction, 0, 1);
        if (!pointer.has_value()) {
            return false;
        }
        frame.delta.append_assert_pointer(*pointer);
        return true;
    }
    case InstructionOp::AssertFact: {
        if (!arg_count(frame, index, instruction, 1)) {
            return false;
        }
        const auto hash = as_hash(instruction.args[0]);
        if (!hash.has_value()) {
            add_diag(frame, index, "E_INVALID_ARG", "AssertFact argument 0 must be Hash");
            return false;
        }
        const auto fact = FactId{*hash};
        if (!contains_fact(frame.before, fact)) {
            add_diag(frame, index, "E_ASSERT_FACT", fmt::format("fact {} does not exist", to_string(fact)));
            return false;
        }
        frame.delta.append_assert_fact(fact);
        return true;
    }
    case InstructionOp::EmitEvent: {
        if (instruction.args.size() < 3) {
            add_diag(frame, index, "E_INVALID_ARITY", "EmitEvent requires at least event, field count, and measurement count");
            return false;
        }
        const auto event_name = string_symbol(frame, index, instruction, 0);
        const auto field_count = u64_arg(frame, index, instruction, 1);
        if (!event_name.has_value() || !field_count.has_value()) {
            return false;
        }
        std::size_t cursor = 2;
        TraceEvent event;
        event.event = *event_name;
        for (std::uint64_t field = 0; field < *field_count; ++field) {
            if (cursor + 1 >= instruction.args.size()) {
                add_diag(frame, index, "E_INVALID_ARITY", "EmitEvent field list is truncated");
                return false;
            }
            const auto key = string_symbol(frame, index, instruction, cursor++);
            const auto value = value_arg(frame, index, instruction, cursor++);
            if (!key.has_value() || !value.has_value()) {
                return false;
            }
            event.fields.emplace(*key, value_to_field(*value));
        }
        if (cursor >= instruction.args.size()) {
            add_diag(frame, index, "E_INVALID_ARITY", "EmitEvent missing measurement count");
            return false;
        }
        const auto measurement_count = as_u64(instruction.args[cursor++]);
        if (!measurement_count.has_value()) {
            add_diag(frame, index, "E_INVALID_ARG", "EmitEvent measurement count must be UInt64");
            return false;
        }
        for (std::uint64_t measurement = 0; measurement < *measurement_count; ++measurement) {
            if (cursor + 1 >= instruction.args.size()) {
                add_diag(frame, index, "E_INVALID_ARITY", "EmitEvent measurement list is truncated");
                return false;
            }
            const auto key = string_symbol(frame, index, instruction, cursor++);
            const auto raw_value = value_arg(frame, index, instruction, cursor++);
            if (!key.has_value() || !raw_value.has_value()) {
                return false;
            }
            const auto value = value_to_measurement(*raw_value);
            if (!value.has_value()) {
                add_diag(frame, index, "E_INVALID_ARG", "EmitEvent measurement values must be numeric");
                return false;
            }
            event.measurements.emplace(*key, *value);
        }
        if (cursor != instruction.args.size()) {
            add_diag(frame, index, "E_INVALID_ARITY", "EmitEvent has trailing operands");
            return false;
        }
        frame.delta.append_event(std::move(event));
        return true;
    }
    }
    add_diag(frame, index, "E_UNKNOWN_OP", "unknown instruction opcode");
    return false;
}

}  // namespace

VmResult KernelVm::execute(const WorldSnapshot& before, const Program& program) const {
    VmFrame frame{before, program};
    for (std::size_t index = 0; index < program.instructions.size(); ++index) {
        if (!execute_instruction(frame, index, program.instructions[index])) {
            VmResult result;
            result.delta = std::move(frame.delta);
            result.diagnostics = std::move(frame.diagnostics);
            return result;
        }
    }

    auto predicted_after = SnapshotOverlay{before}.apply(frame.delta);
    if (!predicted_after.has_value()) {
        add_diag(
            frame,
            program.instructions.empty() ? 0 : program.instructions.size() - 1,
            "E_OVERLAY_REJECTED",
            fmt::format("overlay rejected program: {}", to_string(predicted_after.error())),
            DiagnosticSeverity::Fatal);
        VmResult result;
        result.delta = std::move(frame.delta);
        result.diagnostics = std::move(frame.diagnostics);
        return result;
    }

    Transaction tx;
    tx.origin = TransactionOrigin::Internal;
    tx.label = "kernel vm";
    tx.program = program;
    tx.delta = frame.delta;
    tx.allow_empty = true;

    auto plan = make_execution_plan(tx, before, *predicted_after, VerificationResult{});
    auto sealed = seal_execution_plan(std::move(plan));

    VmResult result;
    result.ok = true;
    result.delta = std::move(frame.delta);
    result.plan = std::move(sealed.plan);
    result.proof = sealed.proof;
    return result;
}

std::string format_vm_diagnostics(std::span<const VmDiagnostic> diagnostics) {
    std::ostringstream out;
    out << "program rejected";
    for (const auto& diagnostic : diagnostics) {
        out << ":\n  instruction " << diagnostic.instruction_index;
        if (!diagnostic.code.empty()) {
            out << ' ' << diagnostic.code;
        }
        out << " [" << to_string(diagnostic.severity) << "]";
        if (!diagnostic.message.empty()) {
            out << ": " << diagnostic.message;
        }
    }
    return out.str();
}

}  // namespace pv
