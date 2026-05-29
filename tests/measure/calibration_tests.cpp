// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <utility>

#include "pv/core/world.hpp"
#include "pv/measure/calibration.hpp"
#include "pv/storage/repository.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_repo_path(std::string_view name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_calibration_" + std::string{name} + "_" + std::to_string(stamp));
    std::filesystem::remove_all(path);
    return path;
}

Transaction object_tx(World& world, std::string name) {
    Transaction tx;
    tx.label = "object " + name;
    tx.delta = world.object_delta(std::move(name), "Node");
    return tx;
}

CommitId append_object(Repository& repo, std::string_view branch, std::string name) {
    auto record = repo.commit(branch, object_tx(repo.mutable_world(branch), std::move(name)), Verifier{});
    REQUIRE(record.has_value());
    return record->id;
}

}  // namespace

TEST_CASE("calibration baseline freezes sample at inclusive up-to commit") {
    const auto root = temp_repo_path("freeze");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    const auto first = append_object(repo, "main", "A");
    const auto second = append_object(repo, "main", "B");
    const auto target = append_object(repo, "main", "C");

    const auto baseline = CalibrationStore{repo}.create(
        "main",
        "main",
        second,
        default_measurement_spec());

    REQUIRE(baseline.up_to_commit == second);
    REQUIRE(baseline.sample.size() == 2);
    REQUIRE(calibration_contains_commit(baseline, first));
    REQUIRE(calibration_contains_commit(baseline, second));
    REQUIRE_FALSE(calibration_contains_commit(baseline, target));
    std::filesystem::remove_all(root);
}

TEST_CASE("calibration baseline persists by name") {
    const auto root = temp_repo_path("show");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    const auto commit = append_object(repo, "main", "A");

    const auto created = CalibrationStore{repo}.create(
        "main",
        "main",
        commit,
        default_measurement_spec());
    auto reopened = Repository::open(root);
    const auto loaded = CalibrationStore{reopened}.load("main");

    REQUIRE(loaded.baseline_hash == created.baseline_hash);
    REQUIRE(loaded.sample.size() == 1);
    REQUIRE(loaded.spec_hash == measurement_spec_hash(default_measurement_spec()));
    std::filesystem::remove_all(root);
}
