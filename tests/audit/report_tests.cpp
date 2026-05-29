// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>

#include <nlohmann/json.hpp>

#include "pv/audit/report.hpp"
#include "pv/audit/timeline.hpp"
#include "pv/hash/canonical.hpp"
#include "pv/ingest/agent_audit_adapter.hpp"
#include "pv/ingest/ingestion_index.hpp"
#include "pv/ingest/ingestion_pipeline.hpp"
#include "pv/measure/risk_projection.hpp"
#include "pv/storage/repository.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_repo_path(std::string_view name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_audit_" + std::string{name} + "_" + std::to_string(stamp));
    std::filesystem::remove_all(path);
    return path;
}

EvidenceEvent create_pr_event() {
    EvidenceEvent event;
    event.source = "agent-log";
    event.event_id = "1";
    event.actor = "Agent0";
    event.action = "create_pr";
    event.target = "PR42";
    return event;
}

}  // namespace

TEST_CASE("audit report renders text and JSON from observed violations") {
    const auto root = temp_repo_path("report");
    auto repository = Repository::init(root);
    IngestionIndex index{repository.root()};
    IngestionOptions options;
    options.branch = "main";
    options.mode = VerificationMode::Observe;

    const auto result = IngestionPipeline{repository}.ingest({create_pr_event()}, AgentAuditAdapter{}, index, options);
    REQUIRE(result.accepted == 1);
    REQUIRE(result.violations == 1);

    const auto report = AuditReportGenerator{}.generate(repository, "main");
    REQUIRE(report.branch == "main");
    REQUIRE(report.commits_checked == 1);
    REQUIRE(report.violations.size() == 1);
    REQUIRE(report.violations.front().law == "no_pr_without_tests");
    REQUIRE(report.risk.law_distance == 1);
    REQUIRE(report.projected_score == project(report.risk));
    REQUIRE(report.risk_score == static_cast<int>(std::min<std::uint64_t>(report.projected_score, 100)));
    REQUIRE_FALSE(report.measured_risks.empty());
    REQUIRE_FALSE(empty(report.measurement_spec_hash));

    const auto text = render_audit_report_text(report);
    REQUIRE(text.find("Audit report: main") != std::string::npos);
    REQUIRE(text.find("no_pr_without_tests") != std::string::npos);
    REQUIRE(text.find("risk vector: structural=") != std::string::npos);

    const auto json = nlohmann::json::parse(render_audit_report_json(report));
    REQUIRE(json["branch"] == "main");
    REQUIRE(json["commits_checked"] == 1);
    REQUIRE(json["risk"]["law_distance"] == 1);
    REQUIRE(json["projected_score"] == report.projected_score);
    REQUIRE(json["projection_hash"].get<std::string>().size() == 64);
    REQUIRE(json["measurement_spec_hash"].get<std::string>().size() == 64);
    REQUIRE(json["measured_risks"].size() == 1);
    REQUIRE(json["violations"].size() == 1);
    REQUIRE(json["violations"][0]["severity"] == "error");

    std::filesystem::remove_all(root);
}

TEST_CASE("audit first-broke finds the earliest commit that broke a law") {
    const auto root = temp_repo_path("firstbroke");
    auto repository = Repository::init(root);
    IngestionIndex index{repository.root()};
    IngestionOptions options;
    options.branch = "main";
    options.mode = VerificationMode::Observe;

    REQUIRE(IngestionPipeline{repository}.ingest({create_pr_event()}, AgentAuditAdapter{}, index, options).violations == 1);

    const auto report = AuditReportGenerator{}.generate(repository, "main");

    const auto broke = first_violation(report, "no_pr_without_tests");
    REQUIRE(broke.has_value());
    REQUIRE(broke->law == "no_pr_without_tests");
    const auto broke_text = render_first_break_text("main", "no_pr_without_tests", broke);
    REQUIRE(broke_text.find("first broke at epoch") != std::string::npos);

    const auto clean = first_violation(report, "no_secret_exposure");
    REQUIRE_FALSE(clean.has_value());
    const auto clean_text = render_first_break_text("main", "no_secret_exposure", clean);
    REQUIRE(clean_text.find("never broke") != std::string::npos);

    std::filesystem::remove_all(root);
}

TEST_CASE("audit timeline lists events touching an object") {
    const auto root = temp_repo_path("timeline");
    auto repository = Repository::init(root);
    IngestionIndex index{repository.root()};
    IngestionOptions options;
    options.branch = "main";
    options.mode = VerificationMode::Observe;

    REQUIRE(IngestionPipeline{repository}.ingest({create_pr_event()}, AgentAuditAdapter{}, index, options).accepted == 1);
    const auto entries = audit_timeline(repository, "main", "Agent0");

    REQUIRE_FALSE(entries.empty());
    const auto text = render_audit_timeline_text("main", "Agent0", entries);
    REQUIRE(text.find("Agent0") != std::string::npos);
    REQUIRE(text.find("evidence.ingest") != std::string::npos);

    std::filesystem::remove_all(root);
}
