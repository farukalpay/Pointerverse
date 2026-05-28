// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#include "pv/hash/canonical.hpp"
#include "pv/kernel/instruction.hpp"

namespace pv {

struct ProgramSymbolTable {
    std::vector<std::string> strings;
    std::vector<std::string> type_names;
    std::vector<std::string> relation_names;
};

struct Program {
    ProgramSymbolTable symbols;
    std::vector<Instruction> instructions;
};

[[nodiscard]] Hash256 symbol_table_hash(const ProgramSymbolTable& symbols);
[[nodiscard]] Hash256 instruction_root(const Program& program);
[[nodiscard]] Hash256 program_hash(const Program& program);

}  // namespace pv
