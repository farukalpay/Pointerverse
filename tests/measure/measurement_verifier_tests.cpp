// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>

#include "pv/core/world.hpp"
#include "pv/kernel/canonical_codec.hpp"
#include "pv/measure/measurement_store.hpp"
#include "pv/measure/measurement_verifier.hpp"
#include "pv/storage/index_store.hpp"
#include "pv/storage/repository.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_repo_path(std::string_view name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_measure_verify_" + std::string{name} + "_" + std::to_string(stamp));
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

void overwrite_file(const std::filesystem::path& path, std::string_view text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
}

}  // namespace

TEST_CASE("measurement verifier accepts clean cached measurements") {
    const auto root = temp_repo_path("clean");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    append_object(repo, "main", "A");
    const auto spec = default_measurement_spec();

    REQUIRE(MeasurementStore{repo}.measure_or_load_branch("main", spec).cache_misses == 1);
    const auto report = MeasurementVerifier{repo}.verify_branch("main", spec);

    REQUIRE(report.clean());
    REQUIRE(report.measurements_checked == 1);
    std::filesystem::remove_all(root);
}

TEST_CASE("measurement verifier detects tampered measurement object") {
    const auto root = temp_repo_path("tamper_record");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    const auto commit = append_object(repo, "main", "A");
    const auto spec = default_measurement_spec();
    const auto measured = MeasurementStore{repo}.measure_or_load_commit("main", commit, spec);

    overwrite_file(repo.objects().object_path(measured.record.measurement_object_hash), "tampered");
    const auto report = MeasurementVerifier{repo}.verify_branch("main", spec);

    REQUIRE_FALSE(report.clean());
    std::filesystem::remove_all(root);
}

TEST_CASE("measurement verifier detects changed evidence root") {
    const auto root = temp_repo_path("tamper_root");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    const auto commit = append_object(repo, "main", "A");
    const auto spec = default_measurement_spec();
    auto store = MeasurementStore{repo};
    auto measured = store.measure_or_load_commit("main", commit, spec);

    measured.record.evidence_root = Hash256{};
    measured.record.measurement_identity_hash = measurement_identity_hash(measured.record);
    measured.record.measurement_object_hash = measurement_record_hash(measured.record);
    measured.record.id = measured.record.measurement_object_hash;
    measured.record.measurement_hash = measured.record.measurement_identity_hash;
    const auto object = repo.objects().put_bytes(canonical_encode(measured.record));
    MeasurementIndex{repo.root()}.upsert("main", MeasurementIndexEntry{
        "main",
        measured.record.commit,
        measured.record.spec_hash,
        object,
        measured.record.measurement_identity_hash,
        measured.record.component_root,
        measured.record.evidence_root,
        measured.record.risk,
        measured.record.projection,
        false
    });

    const auto report = MeasurementVerifier{repo}.verify_branch("main", spec);

    REQUIRE_FALSE(report.clean());
    std::filesystem::remove_all(root);
}

TEST_CASE("measurement verifier detects missing evidence object") {
    const auto root = temp_repo_path("missing_evidence");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    const auto commit = append_object(repo, "main", "A");
    const auto spec = default_measurement_spec();
    const auto measured = MeasurementStore{repo}.measure_or_load_commit("main", commit, spec);
    REQUIRE_FALSE(measured.record.evidence_objects.empty());

    std::filesystem::remove(repo.objects().object_path(measured.record.evidence_objects.front()));
    const auto report = MeasurementVerifier{repo}.verify_branch("main", spec);

    REQUIRE_FALSE(report.clean());
    std::filesystem::remove_all(root);
}

TEST_CASE("measurement verifier strict cache fails stale legacy index entries") {
    const auto root = temp_repo_path("strict_stale");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"seed"});
    const auto commit = append_object(repo, "main", "A");
    auto store = MeasurementStore{repo};
    const auto spec = default_measurement_spec();
    const auto spec_hash = store.put_spec(spec);

    IndexPayloadWriter writer;
    writer.u64(1);
    writer.string("main");
    writer.hash(commit.value);
    writer.hash(spec_hash);
    writer.hash(Hash256{});
    writer.u64(1);
    writer.u64(0);
    writer.u64(0);
    writer.u64(0);
    writer.u64(1);
    IndexStore{repo.root(), "measurements.idx", "PVMEASUREIDX1"}.write_payload(writer.bytes());

    const auto soft = MeasurementVerifier{repo}.verify_branch("main", spec);
    REQUIRE(soft.clean());
    REQUIRE_FALSE(soft.warnings.empty());

    const auto strict = MeasurementVerifier{repo}.verify_branch(
        "main",
        spec,
        nullptr,
        MeasurementVerificationOptions{true});
    REQUIRE_FALSE(strict.clean());
    std::filesystem::remove_all(root);
}
