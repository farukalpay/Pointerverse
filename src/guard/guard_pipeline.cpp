// SPDX-License-Identifier: Apache-2.0
#include "pv/guard/guard_pipeline.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "pv/guard/git_diff_adapter.hpp"
#include "pv/guard/policy_pack.hpp"
#include "pv/ingest/agent_audit_adapter.hpp"
#include "pv/ingest/ingestion_index.hpp"
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

}  // namespace

bool guard_strict_failed(const GuardReport& report) noexcept {
    if (report.risk_score >= 70) {
        return true;
    }
    return std::ranges::any_of(report.findings, [](const GuardFinding& finding) {
        return finding.severity == FindingSeverity::High || finding.severity == FindingSeverity::Critical;
    });
}

GuardRunResult run_guard(const GuardRunOptions& options) {
    validate_options(options);

    const auto repo = absolute_existing_or_lexical(options.repo);
    const auto store = store_path_for(options, repo);
    const auto entries = GitDiffAdapter{}.read(GitDiffOptions{repo, options.base, options.head});
    const auto events = evidence_from_git_diff(entries);

    auto repository = open_or_init_repository(store);
    IngestionIndex index{repository.root()};
    IngestionOptions ingestion_options;
    ingestion_options.branch = options.branch;
    ingestion_options.domain = "agent_audit";
    ingestion_options.mode = options.mode == "strict" ? VerificationMode::Strict : VerificationMode::Observe;
    auto ingestion = IngestionPipeline{repository}.ingest(events, AgentAuditAdapter{}, index, ingestion_options);

    auto findings = PrGuardPolicyPack{}.evaluate(entries);
    const auto commits = evidence_commits(repository, options.branch);
    attach_commits(findings, commits);

    GuardReport report;
    report.repo = repo.string();
    report.base = options.base;
    report.head = options.head;
    report.mode = options.mode;
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
    report.risk_score = guard_risk_score(report.findings);
    report.status = guard_status_for_risk(report.risk_score);

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
