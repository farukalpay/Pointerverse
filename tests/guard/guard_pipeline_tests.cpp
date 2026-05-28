// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>

#include <nlohmann/json.hpp>

#include "pv/guard/guard_pipeline.hpp"
#include "pv/guard/report.hpp"
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

}  // namespace

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
    REQUIRE(result.report.risk_score >= 70);
    REQUIRE((result.report.status == "risky" || result.report.status == "critical"));
    REQUIRE(result.ingestion.accepted == result.report.changed_files);
    REQUIRE(std::filesystem::exists(root / "audit-report.md"));
    const auto markdown = read_file(root / "audit-report.md");
    REQUIRE(markdown.find("## Pointerverse Guard") != std::string::npos);
    REQUIRE(markdown.find("### Artifacts") != std::string::npos);
    REQUIRE(markdown.find("secret_pattern_in_diff_is_critical") != std::string::npos);

    const auto repository = Repository::open(root / "pvstore");
    REQUIRE(repository.has_branch("guard"));

    const auto json = nlohmann::json::parse(render_guard_report_json(result.report));
    REQUIRE(json["tool"] == "Pointerverse Guard");
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

    std::filesystem::remove_all(root);
}
