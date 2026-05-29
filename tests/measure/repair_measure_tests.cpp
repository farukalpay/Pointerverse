// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <variant>

#include "pv/core/world.hpp"
#include "pv/measure/repair_measure.hpp"
#include "pv/storage/repository.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_repo_path(std::string_view name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_measure_repair_" + std::string{name} + "_" + std::to_string(stamp));
    std::filesystem::remove_all(path);
    return path;
}

Transaction object_tx(World& world, std::string name) {
    Transaction tx;
    tx.label = "object " + name;
    tx.delta = world.object_delta(std::move(name), "Node");
    return tx;
}

Transaction link_tx(World& world, double weight) {
    Transaction tx;
    tx.label = "link";
    tx.delta = world.link_delta(world.object_by_name("A"), world.object_by_name("B"), "causes", weight, CausalRole::Structural);
    return tx;
}

}  // namespace

TEST_CASE("repair distance is zero for legal state and positive for illegal state") {
    const auto root = temp_repo_path("distance");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});

    Verifier observe{VerificationMode::Observe};
    observe.add_builtin("bounded_weight");

    const auto clean = repo.commit("main", object_tx(repo.mutable_world("main"), "A"), observe);
    REQUIRE(clean.has_value());
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "B"), observe).has_value());
    const auto illegal = repo.commit("main", link_tx(repo.mutable_world("main"), 1.5), observe);
    REQUIRE(illegal.has_value());
    REQUIRE_FALSE(illegal->violations.empty());

    const auto clean_repair = RepairDistanceMeasure{}.measure(repo, "main", clean->id, observe);
    const auto illegal_repair = RepairDistanceMeasure{}.measure(repo, "main", illegal->id, observe);

    REQUIRE(clean_repair.value == 0);
    REQUIRE(illegal_repair.value > 0);
    REQUIRE(illegal_repair.evidence.explanation.find("repair basis hash:") != std::string::npos);
    REQUIRE(illegal_repair.evidence.explanation.find("minimum witness operation batch hash:") != std::string::npos);

    std::filesystem::remove_all(root);
}

TEST_CASE("repair solver returns minimum legal witness delta for illegal state") {
    const auto root = temp_repo_path("solver_witness");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});

    Verifier observe{VerificationMode::Observe};
    observe.add_builtin("bounded_weight");

    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "A"), observe).has_value());
    REQUIRE(repo.commit("main", object_tx(repo.mutable_world("main"), "B"), observe).has_value());
    const auto illegal = repo.commit("main", link_tx(repo.mutable_world("main"), 1.5), observe);
    REQUIRE(illegal.has_value());
    REQUIRE_FALSE(illegal->violations.empty());

    const auto solved = RepairSolver{}.solve(repo, "main", illegal->id, observe);

    REQUIRE(solved.status == RepairSolveStatus::Found);
    REQUIRE(solved.depth == 1);
    REQUIRE(solved.witness.delta.ops.size() == 1);
    REQUIRE(solved.witness.operation_hashes.size() == 1);
    REQUIRE(solved.witness.operation_batch_hash == repair_operation_batch_hash(solved.witness.operation_hashes));

    const auto& op = solved.witness.delta.ops.front();
    REQUIRE(op.kind == OperationKind::SetPointerWeight);
    const auto& weight = std::get<SetPointerWeightOp>(op.body);
    REQUIRE(weight.weight.value == 1.0);

    const auto target = repo.backend().snapshot(illegal->id);
    const auto repaired = apply_delta_to_snapshot(target, solved.witness.delta);
    REQUIRE(repaired.has_value());
    const auto repaired_check = observe.check(LawCheckContext{target, solved.witness.delta, *repaired});
    REQUIRE(repaired_check.violations.empty());

    std::filesystem::remove_all(root);
}

TEST_CASE("repair solver returns empty witness for legal state") {
    const auto root = temp_repo_path("solver_legal");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});

    Verifier observe{VerificationMode::Observe};
    observe.add_builtin("bounded_weight");

    const auto clean = repo.commit("main", object_tx(repo.mutable_world("main"), "A"), observe);
    REQUIRE(clean.has_value());

    const auto solved = RepairSolver{}.solve(repo, "main", clean->id, observe);

    REQUIRE(solved.status == RepairSolveStatus::Accepted);
    REQUIRE(solved.depth == 0);
    REQUIRE(solved.witness.delta.empty());
    REQUIRE(solved.witness.operation_hashes.empty());

    std::filesystem::remove_all(root);
}

TEST_CASE("repair basis hash changes when repair operators change") {
    RepairOperator expire;
    expire.name = "expire_pointer";
    expire.pointer = PointerId{1};
    expire.delta.append_unlink(PointerRemove{PointerId{1}});

    RepairOperator lower;
    lower.name = "lower_weight_to_bound";
    lower.pointer = PointerId{1};
    lower.attribute = "weight";
    lower.delta.append_set_pointer_weight(PointerId{1}, Weight{1.0});

    RepairBasis left;
    left.operators = {expire};
    left.basis_hash = repair_basis_hash(left);

    RepairBasis right;
    right.operators = {expire, lower};
    right.basis_hash = repair_basis_hash(right);

    REQUIRE(left.basis_hash != right.basis_hash);
}
