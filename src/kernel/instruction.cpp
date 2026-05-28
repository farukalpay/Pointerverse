// SPDX-License-Identifier: Apache-2.0
#include "pv/kernel/instruction.hpp"

namespace pv {

std::string to_string(InstructionOp op) {
    switch (op) {
    case InstructionOp::InternType:
        return "InternType";
    case InstructionOp::InternRelation:
        return "InternRelation";
    case InstructionOp::CreateObject:
        return "CreateObject";
    case InstructionOp::SetObjectType:
        return "SetObjectType";
    case InstructionOp::SetObjectExistence:
        return "SetObjectExistence";
    case InstructionOp::SetObjectAttribute:
        return "SetObjectAttribute";
    case InstructionOp::RemoveObjectAttribute:
        return "RemoveObjectAttribute";
    case InstructionOp::CreatePointer:
        return "CreatePointer";
    case InstructionOp::SetPointerWeight:
        return "SetPointerWeight";
    case InstructionOp::SetPointerAttribute:
        return "SetPointerAttribute";
    case InstructionOp::RemovePointerAttribute:
        return "RemovePointerAttribute";
    case InstructionOp::ExpirePointer:
        return "ExpirePointer";
    case InstructionOp::AssertObject:
        return "AssertObject";
    case InstructionOp::AssertPointer:
        return "AssertPointer";
    case InstructionOp::AssertFact:
        return "AssertFact";
    case InstructionOp::EmitEvent:
        return "EmitEvent";
    }
    return "EmitEvent";
}

std::string to_string(DiagnosticSeverity severity) {
    switch (severity) {
    case DiagnosticSeverity::Note:
        return "note";
    case DiagnosticSeverity::Warning:
        return "warning";
    case DiagnosticSeverity::Error:
        return "error";
    case DiagnosticSeverity::Fatal:
        return "fatal";
    }
    return "error";
}

}  // namespace pv
