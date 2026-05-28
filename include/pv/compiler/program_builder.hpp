// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include "pv/core/delta.hpp"
#include "pv/kernel/program.hpp"

namespace pv {

struct ProgramObjectRef {
    std::uint64_t tag{1};
    std::uint64_t value{0};
};

struct ProgramPointerRef {
    std::uint64_t tag{1};
    std::uint64_t value{0};
};

class ProgramBuilder {
public:
    void import_symbols(const WorldSnapshot& snapshot);

    [[nodiscard]] std::uint64_t string_symbol(std::string_view value);
    [[nodiscard]] std::uint64_t type_symbol(std::string_view value);
    [[nodiscard]] std::uint64_t relation_symbol(std::string_view value);

    void intern_type(std::string_view name);
    void intern_relation(std::string_view name);

    [[nodiscard]] ProgramObjectRef object_by_name(std::string_view name);
    [[nodiscard]] ProgramObjectRef temp_object(std::uint32_t temp);
    [[nodiscard]] ProgramPointerRef pointer_by_id(PointerId id);
    [[nodiscard]] ProgramPointerRef temp_pointer(std::uint64_t temp);

    [[nodiscard]] ProgramObjectRef create_object(std::string_view name, std::string_view type);
    [[nodiscard]] ProgramObjectRef ensure_object(const WorldSnapshot& snapshot, std::string_view name, std::string_view type);
    [[nodiscard]] ProgramPointerRef create_pointer(
        ProgramObjectRef from,
        ProgramObjectRef to,
        std::string_view relation,
        double weight = 1.0,
        CausalRole role = CausalRole::Structural,
        std::string_view law_domain = "core");

    void set_object_type(ProgramObjectRef object, std::string_view type);
    void set_object_existence(ProgramObjectRef object, ExistenceState existence);
    void set_object_attribute(ProgramObjectRef object, std::string_view key, Value value);
    void remove_object_attribute(ProgramObjectRef object, std::string_view key);
    void set_pointer_weight(ProgramPointerRef pointer, double weight);
    void set_pointer_attribute(ProgramPointerRef pointer, std::string_view key, Value value);
    void remove_pointer_attribute(ProgramPointerRef pointer, std::string_view key);
    void expire_pointer(ProgramPointerRef pointer);
    void assert_object(ProgramObjectRef object);
    void assert_pointer(ProgramPointerRef pointer);
    void assert_fact(FactId fact);
    void emit_event(const TraceEvent& event);

    void append_delta(const WorldSnapshot& snapshot, const Delta& delta);

    [[nodiscard]] Program build() const;

private:
    void emit(InstructionOp op, std::vector<Value> args);
    [[nodiscard]] ProgramObjectRef ref_for(const WorldSnapshot& snapshot, const ObjectRef& ref);
    [[nodiscard]] std::string type_name(const WorldSnapshot& snapshot, TypeId type) const;
    [[nodiscard]] std::string relation_name(const WorldSnapshot& snapshot, RelationType relation) const;

    Program program_;
    std::unordered_map<std::string, std::uint64_t> string_symbols_;
    std::unordered_map<std::string, std::uint64_t> type_symbols_;
    std::unordered_map<std::string, std::uint64_t> relation_symbols_;
    std::unordered_map<std::uint32_t, ProgramObjectRef> temp_objects_;
    std::unordered_map<std::string, ProgramObjectRef> created_names_;
    std::uint32_t next_temp_object_{1};
    std::uint64_t next_temp_pointer_{1};
};

}  // namespace pv
