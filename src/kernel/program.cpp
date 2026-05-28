// SPDX-License-Identifier: Apache-2.0
#include "pv/kernel/program.hpp"

#include "pv/hash/hasher.hpp"
#include "pv/storage/canonical_codec.hpp"

namespace pv {
namespace {

Hash256 hash_writer(const CanonicalWriter& writer) {
    return sha256(writer.bytes());
}

Hash256 instruction_hash(const Instruction& instruction) {
    CanonicalWriter writer;
    encode(writer, instruction);
    return hash_writer(writer);
}

}  // namespace

Hash256 symbol_table_hash(const ProgramSymbolTable& symbols) {
    CanonicalWriter writer;
    encode(writer, symbols);
    return hash_writer(writer);
}

Hash256 instruction_root(const Program& program) {
    CanonicalWriter writer;
    writer.string("InstructionRoot:v1");
    writer.u64(program.instructions.size());
    for (const auto& instruction : program.instructions) {
        writer.hash(instruction_hash(instruction));
    }
    return hash_writer(writer);
}

Hash256 program_hash(const Program& program) {
    CanonicalWriter writer;
    encode(writer, program);
    return hash_writer(writer);
}

}  // namespace pv
