// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <utility>

#include "pv/core/world.hpp"
#include "pv/measure/measurement_store.hpp"
#include "pv/storage/repository.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_repo_path(std::string_view name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_measure_store_" + std::string{name} + "_" + std::to_string(stamp));
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

TEST_CASE("measurement store preserves same commit and spec hash after reopen") {
    const auto root = temp_repo_path("reopen");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    const auto commit = append_object(repo, "main", "A");
    const auto spec = default_measurement_spec();

    const auto before = MeasurementStore{repo}.measure_or_load_commit("main", commit, spec);
    auto reopened = Repository::open(root);
    const auto after = MeasurementStore{reopened}.measure_or_load_commit("main", commit, spec);

    REQUIRE(after.cache_hit);
    REQUIRE(after.record.measurement_hash == before.record.measurement_hash);
    REQUIRE(after.record.spec_hash == before.record.spec_hash);
    std::filesystem::remove_all(root);
}

TEST_CASE("measurement store changes object identity when spec changes") {
    const auto root = temp_repo_path("spec");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    const auto commit = append_object(repo, "main", "A");

    auto left_spec = default_measurement_spec();
    auto right_spec = default_measurement_spec();
    right_spec.version = 2;

    auto store = MeasurementStore{repo};
    const auto left = store.measure_or_load_commit("main", commit, left_spec);
    const auto right = store.measure_or_load_commit("main", commit, right_spec);

    REQUIRE(left.record.spec_hash != right.record.spec_hash);
    REQUIRE(left.record.measurement_hash != right.record.measurement_hash);
    std::filesystem::remove_all(root);
}

TEST_CASE("measurement index avoids recomputing unchanged branch commits") {
    const auto root = temp_repo_path("cache");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    append_object(repo, "main", "A");
    append_object(repo, "main", "B");

    auto store = MeasurementStore{repo};
    const auto spec = default_measurement_spec();
    const auto first = store.measure_or_load_branch("main", spec);
    const auto second = store.measure_or_load_branch("main", spec);

    REQUIRE(first.cache_misses == 2);
    REQUIRE(first.cache_hits == 0);
    REQUIRE(second.cache_hits == 2);
    REQUIRE(second.cache_misses == 0);
    std::filesystem::remove_all(root);
}

TEST_CASE("measurement cache rebuild rewrites branch entries") {
    const auto root = temp_repo_path("rebuild");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    append_object(repo, "main", "A");
    append_object(repo, "main", "B");

    auto store = MeasurementStore{repo};
    const auto spec = default_measurement_spec();
    const auto rebuilt = store.rebuild_cache("main", spec);

    REQUIRE(rebuilt.cache_misses == 2);
    REQUIRE(store.index().branch_entries("main", measurement_spec_hash(spec)).size() == 2);
    std::filesystem::remove_all(root);
}
