// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <utility>

#include "pv/compiler/script_compiler.hpp"
#include "pv/kernel/vm.hpp"
#include "pv/sentinel/boot_gate.hpp"
#include "pv/sentinel/fault_injection.hpp"
#include "pv/sentinel/patrol.hpp"
#include "pv/storage/repository.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_repo_path(std::string_view name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_sentinel_fault_" + std::string{name} + "_" + std::to_string(stamp));
    std::filesystem::remove_all(path);
    return path;
}

Transaction object_tx(World& world, std::string name, std::string type) {
    Transaction tx;
    tx.label = "object " + name;
    tx.delta = world.object_delta(std::move(name), type);
    return tx;
}

Transaction program_tx(const World& world) {
    auto program = ScriptCompiler{}.compile_object(world.snapshot(), "Agent0", "Agent");
    const auto vm = KernelVm{}.execute(world.snapshot(), program);
    REQUIRE(vm.ok);

    Transaction tx;
    tx.origin = TransactionOrigin::Script;
    tx.label = "object Agent0";
    tx.program = std::move(program);
    tx.delta = vm.delta;
    return tx;
}

}  // namespace

TEST_CASE("fault injection refuses to mutate without explicit confirmation") {
    const auto root = temp_repo_path("no_confirm");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "A", "Node"), Verifier{})->accepted);

    FaultInjectionOptions options;
    options.root = root;
    options.branch = "main";
    options.commit = "HEAD";
    options.kind = FaultObjectKind::Snapshot;

    REQUIRE_THROWS(corrupt_object_fault(options));
    std::filesystem::remove_all(root);
}

TEST_CASE("remove-program fault is detected by VM replay patrol") {
    const auto root = temp_repo_path("remove_program");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    auto tx = program_tx(repo.world("main"));
    REQUIRE(repo.commit("main", std::move(tx), Verifier{})->accepted);

    FaultInjectionOptions options;
    options.root = root;
    options.branch = "main";
    options.commit = "HEAD";
    options.confirm_mutates_store = true;
    REQUIRE(remove_program_fault(options).mutated);

    const auto reopened = Repository::open(root);
    const auto report = patrol_repository(reopened);

    REQUIRE_FALSE(report.clean());
    REQUIRE(report.program_replays == 1);
    std::filesystem::remove_all(root);
}

TEST_CASE("rewrite-ref fault is detected during boot branch ref checks") {
    const auto root = temp_repo_path("rewrite_ref");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});

    FaultInjectionOptions options;
    options.root = root;
    options.branch = "main";
    options.confirm_mutates_store = true;
    REQUIRE(rewrite_ref_fault(options).mutated);

    const auto result = run_boot_gate(root);

    REQUIRE_FALSE(result.ok);
    REQUIRE(result.failed_at == BootStage::BranchRefs);
    std::filesystem::remove_all(root);
}

TEST_CASE("corrupt-object fault is detected during boot object store checks") {
    const auto root = temp_repo_path("corrupt_object");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "A", "Node"), Verifier{})->accepted);

    FaultInjectionOptions options;
    options.root = root;
    options.branch = "main";
    options.commit = "HEAD";
    options.kind = FaultObjectKind::Snapshot;
    options.confirm_mutates_store = true;
    REQUIRE(corrupt_object_fault(options).mutated);

    const auto result = run_boot_gate(root);

    REQUIRE_FALSE(result.ok);
    REQUIRE(result.failed_at == BootStage::ObjectStore);
    std::filesystem::remove_all(root);
}
