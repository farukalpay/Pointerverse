// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "pv/core/value.hpp"

namespace pv {

enum class InstructionOp : std::uint8_t {
    InternType,
    InternRelation,

    CreateObject,
    SetObjectType,
    SetObjectExistence,
    SetObjectAttribute,
    RemoveObjectAttribute,

    CreatePointer,
    SetPointerWeight,
    SetPointerAttribute,
    RemovePointerAttribute,
    ExpirePointer,

    AssertObject,
    AssertPointer,
    AssertFact,

    EmitEvent
};

struct Instruction {
    InstructionOp op{InstructionOp::EmitEvent};
    std::vector<Value> args;
};

enum class DiagnosticSeverity : std::uint8_t {
    Note,
    Warning,
    Error,
    Fatal
};

struct VmDiagnostic {
    DiagnosticSeverity severity{DiagnosticSeverity::Error};
    std::size_t instruction_index{0};
    std::string code;
    std::string message;
};

[[nodiscard]] std::string to_string(InstructionOp op);
[[nodiscard]] std::string to_string(DiagnosticSeverity severity);

}  // namespace pv
