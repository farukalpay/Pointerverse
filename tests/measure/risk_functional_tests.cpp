// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "pv/core/world.hpp"
#include "pv/measure/risk_functional.hpp"
#include "pv/storage/repository.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_repo_path(std::string_view name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_measure_functional_" + std::string{name} + "_" + std::to_string(stamp));
    std::filesystem::remove_all(path);
    return path;
}

Transaction object_tx(World& world, std::string name) {
    Transaction tx;
    tx.label = "object " + name;
    tx.delta = world.object_delta(std::move(name), "Node");
    return tx;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    return std::string{std::istreambuf_iterator<char>{input}, {}};
}

std::vector<std::filesystem::path> files_under(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> out;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && (entry.path().extension() == ".hpp" || entry.path().extension() == ".cpp")) {
            out.push_back(entry.path());
        }
    }
    return out;
}

}  // namespace

TEST_CASE("same repo and same commit gives same measurement hash after reopen") {
    const auto root = temp_repo_path("reopen");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    const auto record = repo.commit("main", object_tx(repo.mutable_world("main"), "A"), Verifier{});
    REQUIRE(record.has_value());

    const auto before = MeasuredRiskFunctional{}.measure_commit(repo, "main", record->id).measurement_hash;
    const auto reopened = Repository::open(root);
    const auto after = MeasuredRiskFunctional{}.measure_commit(reopened, "main", record->id).measurement_hash;

    REQUIRE(after == before);
    std::filesystem::remove_all(root);
}

TEST_CASE("projection policy does not affect measured risk hash") {
    const auto root = temp_repo_path("projection");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    const auto record = repo.commit("main", object_tx(repo.mutable_world("main"), "A"), Verifier{});
    REQUIRE(record.has_value());

    auto left_spec = default_measurement_spec();
    auto right_spec = default_measurement_spec();
    right_spec.projection.structural_weight = 7;

    const auto left = MeasuredRiskFunctional{}.measure_commit(repo, "main", record->id, left_spec);
    const auto right = MeasuredRiskFunctional{}.measure_commit(repo, "main", record->id, right_spec);

    REQUIRE(left.measurement_hash == right.measurement_hash);
    REQUIRE(left.value == right.value);
    REQUIRE(left.projection_result.projection_hash != right.projection_result.projection_hash);
    std::filesystem::remove_all(root);
}

TEST_CASE("projection hash changes when measured object changes") {
    const auto root = temp_repo_path("projection_measurement");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    const auto first = repo.commit("main", object_tx(repo.mutable_world("main"), "A"), Verifier{});
    REQUIRE(first.has_value());
    const auto second = repo.commit("main", object_tx(repo.mutable_world("main"), "B"), Verifier{});
    REQUIRE(second.has_value());

    const auto spec = default_measurement_spec();
    const auto left = MeasuredRiskFunctional{}.measure_commit(repo, "main", first->id, spec);
    const auto right = MeasuredRiskFunctional{}.measure_commit(repo, "main", second->id, spec);

    REQUIRE(left.measurement_hash != right.measurement_hash);
    REQUIRE(left.projection_result.projection_hash != right.projection_result.projection_hash);
    std::filesystem::remove_all(root);
}

TEST_CASE("same operations in different ingestion order produce same joined risk") {
    const auto left_root = temp_repo_path("left");
    const auto right_root = temp_repo_path("right");
    auto left = Repository::init(left_root);
    auto right = Repository::init(right_root);
    (void)left.create_branch("main", World{"seed"});
    (void)right.create_branch("main", World{"seed"});

    REQUIRE(left.commit("main", object_tx(left.mutable_world("main"), "A"), Verifier{}).has_value());
    REQUIRE(left.commit("main", object_tx(left.mutable_world("main"), "B"), Verifier{}).has_value());
    REQUIRE(right.commit("main", object_tx(right.mutable_world("main"), "B"), Verifier{}).has_value());
    REQUIRE(right.commit("main", object_tx(right.mutable_world("main"), "A"), Verifier{}).has_value());

    const auto left_risk = joined_risk(MeasuredRiskFunctional{}.measure_branch(left, "main"));
    const auto right_risk = joined_risk(MeasuredRiskFunctional{}.measure_branch(right, "main"));

    REQUIRE(left_risk == right_risk);
    std::filesystem::remove_all(left_root);
    std::filesystem::remove_all(right_root);
}

TEST_CASE("measured risk path does not depend on guard or audit severity scores") {
    const auto root = std::filesystem::path{POINTERVERSE_SOURCE_ROOT};
    for (const auto& file : files_under(root / "include" / "pv" / "measure")) {
        const auto text = read_file(file);
        CAPTURE(file.string());
        REQUIRE(text.find("pv/guard/") == std::string::npos);
        REQUIRE(text.find("pv/audit/") == std::string::npos);
        REQUIRE(text.find("risk_points") == std::string::npos);
    }
    for (const auto& file : files_under(root / "src" / "measure")) {
        const auto text = read_file(file);
        CAPTURE(file.string());
        REQUIRE(text.find("pv/guard/") == std::string::npos);
        REQUIRE(text.find("pv/audit/") == std::string::npos);
        REQUIRE(text.find("risk_points") == std::string::npos);
    }
}
