// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>

#include "pv/compiler/script_compiler.hpp"
#include "pv/kernel/merkle.hpp"
#include "pv/kernel/vm.hpp"
#include "pv/runtime/transaction.hpp"
#include "pv/runtime/world_store.hpp"

using namespace pv;

namespace {

Transaction tx_from_program(const World& world, Program program, std::string label = "program") {
    const auto vm = KernelVm{}.execute(world.snapshot(), program);
    REQUIRE(vm.ok);

    Transaction tx;
    tx.origin = TransactionOrigin::Script;
    tx.label = std::move(label);
    tx.program = std::move(program);
    tx.delta = vm.delta;
    return tx;
}

CommitResult commit_program(World& world, Program program, std::string label = "program") {
    auto tx = tx_from_program(world, std::move(program), std::move(label));
    auto prepared = prepare_transaction(world, tx, Verifier{});
    REQUIRE(prepared.committable);
    return commit_prepared(world, prepared);
}

}  // namespace

TEST_CASE("same program produces same world root across fresh repositories") {
    World left{"vm"};
    World right{"vm"};
    const auto program = ScriptCompiler{}.compile_object(left.snapshot(), "Agent0", "Agent");

    REQUIRE(commit_program(left, program).accepted);
    REQUIRE(commit_program(right, program).accepted);

    REQUIRE(compute_world_root(left.snapshot()).root == compute_world_root(right.snapshot()).root);
}

TEST_CASE("program with unresolved object fails before mutation") {
    World world{"vm"};
    const auto before = world.canonical_hash();
    const auto program = ScriptCompiler{}.compile_link(world.snapshot(), "Agent0", "FileA", "modifies", 1.0, CausalRole::Structural);

    const auto vm = KernelVm{}.execute(world.snapshot(), program);

    REQUIRE_FALSE(vm.ok);
    REQUIRE_FALSE(vm.diagnostics.empty());
    REQUIRE(vm.diagnostics.front().code == "E_UNRESOLVED_OBJECT");
    REQUIRE(world.canonical_hash() == before);
}

TEST_CASE("program execution is atomic through transaction prepare") {
    World world{"vm"};
    const auto before = world.canonical_hash();
    const auto program = ScriptCompiler{}.compile_object(world.snapshot(), "Agent0", "Agent");

    Transaction tx;
    tx.origin = TransactionOrigin::Script;
    tx.label = "tampered program";
    tx.program = program;
    tx.delta = Delta{};
    tx.allow_empty = true;

    const auto prepared = prepare_transaction(world, tx, Verifier{});

    REQUIRE_FALSE(prepared.committable);
    REQUIRE(prepared.rejection_reason.find("VM(program).delta") != std::string::npos);
    REQUIRE(world.canonical_hash() == before);
}

TEST_CASE("program replay produces same execution plan hash") {
    World world{"vm"};
    const auto program = ScriptCompiler{}.compile_object(world.snapshot(), "Agent0", "Agent");

    const auto left = KernelVm{}.execute(world.snapshot(), program);
    const auto right = KernelVm{}.execute(world.snapshot(), program);

    REQUIRE(left.ok);
    REQUIRE(right.ok);
    REQUIRE(left.plan.plan_hash == right.plan.plan_hash);
    REQUIRE(left.proof.has_value());
    REQUIRE(right.proof.has_value());
    REQUIRE(left.proof->program_root == program_hash(program));
}

TEST_CASE("compiled script and handwritten operations produce same world root") {
    World compiled{"vm"};
    World handwritten{"vm"};
    Verifier verifier;

    REQUIRE(commit_program(compiled, ScriptCompiler{}.compile_object(compiled.snapshot(), "Agent0", "Agent")).accepted);
    REQUIRE(commit_program(compiled, ScriptCompiler{}.compile_object(compiled.snapshot(), "FileA", "File")).accepted);
    REQUIRE(commit_program(
        compiled,
        ScriptCompiler{}.compile_link(compiled.snapshot(), "Agent0", "FileA", "modifies", 1.0, CausalRole::Structural))
        .accepted);

    REQUIRE(handwritten.commit(handwritten.object_delta("Agent0", "Agent"), verifier).accepted);
    REQUIRE(handwritten.commit(handwritten.object_delta("FileA", "File"), verifier).accepted);
    REQUIRE(handwritten.commit(
        handwritten.link_delta(
            handwritten.object_by_name("Agent0"),
            handwritten.object_by_name("FileA"),
            "modifies",
            1.0,
            CausalRole::Structural),
        verifier)
        .accepted);

    REQUIRE(compute_world_root(compiled.snapshot()).root == compute_world_root(handwritten.snapshot()).root);
}

TEST_CASE("program-backed commits carry program roots") {
    WorldStore store;
    const auto main = store.create_branch("main", World{"vm"});
    const auto program = ScriptCompiler{}.compile_object(store.world(main).snapshot(), "Agent0", "Agent");
    auto tx = tx_from_program(store.world(main), program, "object Agent0");

    const auto record = store.commit(main, tx, Verifier{});

    REQUIRE(record.has_value());
    REQUIRE(record->accepted);
    REQUIRE(record->program_hash == program_hash(program));
    REQUIRE(record->instruction_root == instruction_root(program));
    REQUIRE(record->symbol_table_hash == symbol_table_hash(program.symbols));
    REQUIRE(record->proof.has_value());
    REQUIRE(record->proof->program_root == program_hash(program));
}
