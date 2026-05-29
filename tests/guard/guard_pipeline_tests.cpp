// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>

#include <nlohmann/json.hpp>

#include "pv/guard/guard_pipeline.hpp"
#include "pv/guard/report.hpp"
#include "pv/measure/calibration.hpp"
#include "pv/measure/measurement_spec.hpp"
#include "pv/storage/repository.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_path(std::string_view name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_guard_pipeline_" + std::string{name} + "_" + std::to_string(stamp));
    std::filesystem::remove_all(path);
    return path;
}

void write_file(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    output << text;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    return std::string{std::istreambuf_iterator<char>{input}, {}};
}

void make_fixture(const std::filesystem::path& root) {
    const auto before = root / "before";
    const auto after = root / "after";
    write_file(before / "src" / "auth.cpp", "bool login() { return true; }\n");
    write_file(before / "tests" / "auth_test.cpp", "TEST(Auth, Login) {}\n");
    write_file(before / "package-lock.json", "{\"lockfileVersion\": 3}\n");

    write_file(after / "src" / "auth.cpp", "bool login() { return false; }\n");
    write_file(after / "config" / "dev.env", "SECRET_KEY=demo\n");
    write_file(after / ".github" / "workflows" / "deploy.yml", "name: deploy\n");
    write_file(after / "package-lock.json", "{\"lockfileVersion\": 3, \"packages\": {}}\n");
    write_file(after / "src" / "generated" / "client.cpp", "// @generated\nint client = 1;\n");
}

CommitId fake_commit(std::byte marker) {
    Hash256 hash;
    hash.value.back() = marker;
    return CommitId{hash};
}

}  // namespace

TEST_CASE("guard strict decision uses frozen baseline and rejects current commit contamination") {
    const auto spec = agent_audit_measurement_spec();
    const auto spec_hash = measurement_spec_hash(spec);
    const auto current_commit = fake_commit(std::byte{0x42});

    GuardReport report;
    report.measurement_spec_hash = spec_hash;
    report.measured_risk.structural = 100;
    report.strict_policy.min_history_commits_for_calibration = 1;
    MeasuredRisk current;
    current.commit = current_commit;
    current.value = report.measured_risk;
    report.measured_risks.push_back(current);

    CalibrationBaseline baseline;
    baseline.branch = "guard";
    baseline.spec_hash = spec_hash;
    baseline.sample.push_back(MeasurementIndexEntry{
        "guard",
        fake_commit(std::byte{0x11}),
        spec_hash,
        Hash256{},
        Hash256{},
        Hash256{},
        Hash256{},
        RiskVector{5, 0, 0, 3},
        8,
        false
    });

    const auto decision = strict_decision_for(report, &baseline);

    REQUIRE(decision.calibration_commits == 1);
    REQUIRE(decision.structural_threshold == 5);
    REQUIRE(decision.structural_failed);
    REQUIRE_FALSE(decision.baseline_contaminated);

    baseline.sample.front().commit = current_commit;
    const auto contaminated = strict_decision_for(report, &baseline);
    REQUIRE(contaminated.baseline_contaminated);
    REQUIRE(contaminated.failed);
}

TEST_CASE("guard pipeline ingests diff, persists graph, and renders reports") {
    const auto root = temp_path("run");
    make_fixture(root);

    GuardRunOptions options;
    options.repo = root / "after";
    options.base = "../before";
    options.mode = "observe";
    options.format = "markdown";
    options.out = root / "audit-report.md";
    options.store = root / "pvstore";
    options.write_default_artifacts = false;

    const auto result = run_guard(options);

    REQUIRE(result.report.changed_files >= 5);
    REQUIRE(result.report.measured_risk.law_distance > 0);
    REQUIRE(result.report.projected_score > 0);
    REQUIRE(result.report.status == "risky");
    REQUIRE(result.ingestion.accepted == result.report.changed_files);
    REQUIRE(std::filesystem::exists(root / "audit-report.md"));
    const auto markdown = read_file(root / "audit-report.md");
    REQUIRE(markdown.find("## Pointerverse Guard") != std::string::npos);
    REQUIRE(markdown.find("### Artifacts") != std::string::npos);
    REQUIRE(markdown.find("secret_pattern_in_diff_is_critical") != std::string::npos);

    Hash256 baseline_hash;
    {
        auto repository = Repository::open(root / "pvstore");
        REQUIRE(repository.has_branch("guard"));
        const auto history = repository.history("guard");
        REQUIRE_FALSE(history.empty());
        const auto baseline = CalibrationStore{repository}.create(
            "guard",
            "guard",
            history.back().id,
            agent_audit_measurement_spec());
        baseline_hash = baseline.baseline_hash;
    }

    options.baseline = "guard";
    const auto baseline_result = run_guard(options);
    REQUIRE(baseline_result.report.baseline_hash == baseline_hash);
    REQUIRE(render_guard_report_json(baseline_result.report).find("\"baseline_hash\"") != std::string::npos);

    const auto json = nlohmann::json::parse(render_guard_report_json(result.report));
    REQUIRE(json["tool"] == "Pointerverse Guard");
    REQUIRE(json["measured_risk"]["law_distance"].get<std::uint64_t>() > 0);
    REQUIRE(json["strict_decision"]["failed"] == true);
    REQUIRE(json["findings"].size() >= 4);

    const auto sarif = nlohmann::json::parse(render_guard_report_sarif(result.report));
    REQUIRE(sarif["version"] == "2.1.0");
    REQUIRE(sarif["runs"][0]["results"].size() >= 4);
    bool secret_location_has_line = false;
    for (const auto& item : sarif["runs"][0]["results"]) {
        if (item["ruleId"] != "secret_pattern_in_diff_is_critical") {
            continue;
        }
        const auto& location = item["locations"][0]["physicalLocation"];
        secret_location_has_line =
            location["artifactLocation"]["uri"] == "config/dev.env"
            && location["region"]["startLine"] == 1;
    }
    REQUIRE(secret_location_has_line);

    REQUIRE(guard_strict_failed(result.report));
    REQUIRE(read_file(std::filesystem::path{POINTERVERSE_SOURCE_ROOT} / "src" / "guard" / "guard_pipeline.cpp")
        .find("risk_points") == std::string::npos);

    std::filesystem::remove_all(root);
}
