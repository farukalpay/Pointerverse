// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <utility>

#include "pv/compiler/script_compiler.hpp"
#include "pv/core/world.hpp"
#include "pv/hash/hasher.hpp"
#include "pv/kernel/canonical_codec.hpp"

using namespace pv;

namespace {

Program round_trip(const Program& program) {
    const auto bytes = canonical_encode(program);
    CanonicalReader reader{bytes};
    auto decoded = decode_program(reader);
    reader.expect_end();
    return decoded;
}

}  // namespace

TEST_CASE("program codec round trips canonical instruction streams") {
    World world{"codec"};
    const auto program = ScriptCompiler{}.compile_object(world.snapshot(), "Agent0", "Agent");

    const auto decoded = round_trip(program);

    REQUIRE(program_hash(decoded) == program_hash(program));
    REQUIRE(instruction_root(decoded) == instruction_root(program));
    REQUIRE(symbol_table_hash(decoded.symbols) == symbol_table_hash(program.symbols));
    REQUIRE(sha256(canonical_encode(program)) == program_hash(program));
}

TEST_CASE("program hash changes when instruction order changes") {
    World world{"codec"};
    auto first = ScriptCompiler{}.compile_object(world.snapshot(), "A", "Node");
    auto second = first;
    REQUIRE(second.instructions.size() == 2);
    std::swap(second.instructions[0], second.instructions[1]);

    REQUIRE(program_hash(first) != program_hash(second));
    REQUIRE(instruction_root(first) != instruction_root(second));
    REQUIRE(symbol_table_hash(first.symbols) == symbol_table_hash(second.symbols));
}
