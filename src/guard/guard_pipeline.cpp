// SPDX-License-Identifier: Apache-2.0
#include "pv/guard/guard_pipeline.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "pv/domain/agent_audit.hpp"
#include "pv/guard/git_diff_adapter.hpp"
#include "pv/guard/policy_pack.hpp"
#include "pv/ingest/agent_audit_adapter.hpp"
#include "pv/ingest/ingestion_index.hpp"
#include "pv/measure/calibration.hpp"
#include "pv/measure/measurement_store.hpp"
#include "pv/measure/measurement_spec.hpp"
#include "pv/measure/risk_projection.hpp"
#include "pv/rule/rule_engine.hpp"
#include "pv/runtime/transaction.hpp"
#include "pv/storage/repository.hpp"

namespace pv {
namespace {

std::filesystem::path absolute_existing_or_lexical(std::filesystem::path path) {
    if (path.empty()) {
        path = ".";
    }
    return std::filesystem::weakly_canonical(path);
}

std::filesystem::path store_path_for(const GuardRunOptions& options, const std::filesystem::path& repo) {
    if (options.store.empty()) {
        return repo / ".pvstore";
    }
    if (options.store.is_absolute()) {
        return options.store;
    }
    return repo / options.store;
}

Repository open_or_init_repository(const std::filesystem::path& store) {
    if (std::filesystem::exists(store / "manifest.json")) {
        return Repository::open(store);
    }
    return Repository::init(store);
}

void write_text_file(const std::filesystem::path& path, const std::string& text) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot write guard report artifact");
    }
    output << text;
    output.close();
    if (!output) {
        throw std::runtime_error("failed writing guard report artifact");
    }
}

std::vector<std::string> affected_files(const std::vector<GitDiffEntry>& entries) {
    std::vector<std::string> out;
    out.reserve(entries.size());
    for (const auto& entry : entries) {
        out.push_back(entry.path);
    }
    std::ranges::sort(out);
    out.erase(std::ranges::unique(out).begin(), out.end());
    return out;
}

std::vector<CommitId> evidence_commits(const Repository& repository, std::string_view branch) {
    std::vector<CommitId> out;
    if (!repository.has_branch(branch)) {
        return out;
    }
    for (const auto& record : repository.history(branch)) {
        if (record.origin == TransactionOrigin::Ingestion && record.accepted) {
            out.push_back(record.id);
        }
    }
    return out;
}

std::vector<CommitId> measured_history_ids(const Repository& repository, std::string_view branch) {
    std::vector<CommitId> out;
    if (!repository.has_branch(branch)) {
        return out;
    }
    for (const auto& record : repository.history(branch)) {
        if (record.origin != TransactionOrigin::Internal) {
            out.push_back(record.id);
        }
    }
    return out;
}

void attach_commits(std::vector<GuardFinding>& findings, const std::vector<CommitId>& commits) {
    for (auto& finding : findings) {
        finding.evidence_commits = commits;
    }
}

std::string artifact_label(const std::filesystem::path& path) {
    if (path.empty()) {
        return {};
    }
    return path.lexically_normal().generic_string();
}

void add_artifact(std::vector<std::string>& artifacts, std::string label) {
    if (label.empty()) {
        return;
    }
    if (std::ranges::find(artifacts, label) == artifacts.end()) {
        artifacts.push_back(std::move(label));
    }
}

std::string store_artifact_label(const GuardRunOptions& options) {
    if (options.store.empty()) {
        return ".pvstore/";
    }
    if (options.store.filename() == ".pvstore") {
        return ".pvstore/";
    }
    return artifact_label(options.store);
}

void validate_options(const GuardRunOptions& options) {
    if (options.mode != "observe" && options.mode != "strict") {
        throw std::invalid_argument("mode must be observe or strict");
    }
    (void)render_guard_report(GuardReport{}, options.format);
}

Verifier guard_measure_verifier() {
    const auto package = make_agent_audit_domain();
    RuleEngine rules;
    rules.add_all(package.rules);
    Verifier verifier{VerificationMode::Observe};
    for (const auto& rule : rules.rules()) {
        verifier.add(rules.make_law(rule.name));
    }
    return verifier;
}

std::uint64_t percentile_threshold(std::vector<std::uint64_t> values, double percentile) {
    if (values.empty()) {
        return 0;
    }
    std::ranges::sort(values);
    auto index = static_cast<std::size_t>(std::ceil(percentile * static_cast<double>(values.size())));
    if (index == 0) {
        index = 1;
    }
    return values[std::min(index - 1U, values.size() - 1U)];
}

GuardStrictDecision strict_decision_for_baseline(
    const GuardReport& report,
    const CalibrationBaseline* baseline) {
    GuardStrictDecision decision;
    if (report.strict_policy.fail_on_law_distance && report.measured_risk.law_distance > 0) {
        decision.law_distance_failed = true;
    }
    if (report.strict_policy.fail_on_repair_distance && report.measured_risk.repair_distance > 0) {
        decision.repair_distance_failed = true;
    }

    if (baseline == nullptr) {
        decision.warnings.push_back("structural/surprise calibration unavailable: no frozen baseline supplied");
    } else if (baseline->spec_hash != report.measurement_spec_hash) {
        decision.warnings.push_back("calibration baseline spec does not match current measurement spec");
        decision.failed = true;
    } else {
        decision.calibration_commits = baseline->sample.size();
        for (const auto& measured : report.measured_risks) {
            if (calibration_contains_commit(*baseline, measured.commit)) {
                decision.baseline_contaminated = true;
                decision.warnings.push_back(
                    "calibration baseline contains a current target commit: "
                    + to_hex(measured.commit.value).substr(0, 12));
            }
        }
    }

    if (baseline != nullptr
        && baseline->spec_hash == report.measurement_spec_hash
        && baseline->sample.size() >= report.strict_policy.min_history_commits_for_calibration) {
        std::vector<std::uint64_t> structural;
        std::vector<std::uint64_t> surprise;
        structural.reserve(baseline->sample.size());
        surprise.reserve(baseline->sample.size());
        for (const auto& entry : baseline->sample) {
            structural.push_back(entry.risk.structural);
            surprise.push_back(entry.risk.surprise);
        }
        decision.structural_threshold = percentile_threshold(structural, report.strict_policy.structural_percentile);
        decision.surprise_threshold = percentile_threshold(surprise, report.strict_policy.surprise_percentile);
        decision.structural_failed = report.measured_risk.structural > decision.structural_threshold;
        decision.surprise_failed = report.measured_risk.surprise > decision.surprise_threshold;
    } else if (baseline != nullptr && baseline->spec_hash == report.measurement_spec_hash) {
        decision.warnings.push_back(fmt::format(
            "structural/surprise calibration unavailable: need {} commits, have {}",
            report.strict_policy.min_history_commits_for_calibration,
            baseline->sample.size()));
    }

    decision.failed = decision.law_distance_failed
        || decision.repair_distance_failed
        || decision.structural_failed
        || decision.surprise_failed
        || decision.baseline_contaminated
        || decision.failed;
    return decision;
}

}  // namespace

GuardStrictDecision strict_decision_for(
    const GuardReport& report,
    const CalibrationBaseline* baseline) {
    return strict_decision_for_baseline(report, baseline);
}

bool guard_strict_failed(const GuardReport& report) noexcept {
    return report.strict_decision.failed
        || report.measured_risk.law_distance > 0
        || report.measured_risk.repair_distance > 0;
}

GuardRunResult run_guard(const GuardRunOptions& options) {
    validate_options(options);

    const auto repo = absolute_existing_or_lexical(options.repo);
    const auto store = store_path_for(options, repo);
    const auto entries = GitDiffAdapter{}.read(GitDiffOptions{repo, options.base, options.head});
    const auto events = evidence_from_git_diff(entries);

    auto repository = open_or_init_repository(store);
    const auto previous_history = measured_history_ids(repository, options.branch);
    IngestionIndex index{repository.root()};
    IngestionOptions ingestion_options;
    ingestion_options.branch = options.branch;
    ingestion_options.domain = "agent_audit";
    ingestion_options.mode = VerificationMode::Observe;
    auto ingestion = IngestionPipeline{repository}.ingest(events, AgentAuditAdapter{}, index, ingestion_options);

    auto findings = PrGuardPolicyPack{}.evaluate(entries);
    const auto commits = evidence_commits(repository, options.branch);
    attach_commits(findings, commits);
    auto measure_verifier = guard_measure_verifier();
    const auto measurement_spec = agent_audit_measurement_spec();
    auto measurement_store = MeasurementStore{repository};
    auto measured = measurement_store.measure_new_commits(
        options.branch,
        previous_history,
        measurement_spec,
        &measure_verifier);
    std::optional<CalibrationBaseline> baseline;
    if (!options.baseline.empty()) {
        baseline = CalibrationStore{repository}.load(options.baseline);
    }

    GuardReport report;
    report.repo = repo.string();
    report.base = options.base;
    report.head = options.head;
    report.mode = options.mode;
    report.measurement_spec_hash = measurement_spec_hash(measurement_spec);
    report.baseline = options.baseline;
    if (baseline.has_value()) {
        report.baseline_hash = baseline->baseline_hash;
    }
    report.changed_files = entries.size();
    for (const auto& entry : entries) {
        report.additions += entry.additions;
        report.deletions += entry.deletions;
    }
    report.ingested_events = ingestion.accepted;
    report.ingestion_violations = ingestion.violations;
    report.affected_files = affected_files(entries);
    report.findings = std::move(findings);
    report.evidence_commits = commits;
    report.measured_risks = std::move(measured.measured);
    report.measured_risk = joined_risk(report.measured_risks);
    report.projected_score = project(report.measured_risk, measurement_spec.projection);
    report.risk_score = static_cast<int>(std::min<std::uint64_t>(report.projected_score, 100));
    report.strict_policy = options.strict_policy;
    report.strict_decision = strict_decision_for(report, baseline.has_value() ? &*baseline : nullptr);
    report.status = report.strict_decision.failed ? "risky" : guard_status_for_risk(report.risk_score);

    if (options.write_default_artifacts) {
        report.artifacts = {".pvstore/", "audit-report.md", "audit-report.json", "audit.sarif"};
    } else {
        add_artifact(report.artifacts, artifact_label(options.out));
        add_artifact(report.artifacts, artifact_label(options.markdown_out));
        add_artifact(report.artifacts, artifact_label(options.json_out));
        add_artifact(report.artifacts, artifact_label(options.sarif_out));
        add_artifact(report.artifacts, store_artifact_label(options));
    }

    if (options.write_default_artifacts) {
        write_text_file(repo / "audit-report.md", render_guard_report_markdown(report));
        write_text_file(repo / "audit-report.json", render_guard_report_json(report));
        write_text_file(repo / "audit.sarif", render_guard_report_sarif(report));
    } else {
        if (!options.out.empty()) {
            write_text_file(options.out, render_guard_report(report, options.format));
        }
        if (!options.markdown_out.empty()) {
            write_text_file(options.markdown_out, render_guard_report_markdown(report));
        }
        if (!options.json_out.empty()) {
            write_text_file(options.json_out, render_guard_report_json(report));
        }
        if (!options.sarif_out.empty()) {
            write_text_file(options.sarif_out, render_guard_report_sarif(report));
        }
    }

    GuardRunResult result;
    result.report = std::move(report);
    result.ingestion = std::move(ingestion);
    result.strict_failed = options.mode == "strict" && guard_strict_failed(result.report);
    return result;
}

}  // namespace pv
