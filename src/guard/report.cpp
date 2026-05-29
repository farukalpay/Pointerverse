// SPDX-License-Identifier: Apache-2.0
#include "pv/guard/report.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "pv/hash/canonical.hpp"
#include "pv/measure/risk_projection.hpp"

namespace pv {
namespace {

std::string display_file(const GuardFinding& finding) {
    if (finding.file.empty()) {
        return {};
    }
    if (finding.line.has_value()) {
        return fmt::format("{}:{}", finding.file, *finding.line);
    }
    return finding.file;
}

std::string display_artifact(const std::string& artifact) {
    if (artifact == ".pvstore/" || artifact == ".pvstore") {
        return fmt::format("- `{}` replayable audit graph\n", artifact);
    }
    return fmt::format("- `{}`\n", artifact);
}

std::string upper_severity(FindingSeverity severity) {
    auto value = std::string{to_string(severity)};
    std::ranges::transform(value, value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

nlohmann::json finding_json(const GuardFinding& finding) {
    nlohmann::json json;
    json["severity"] = to_string(finding.severity);
    json["rule"] = finding.rule;
    json["message"] = finding.message;
    json["file"] = finding.file;
    if (finding.line.has_value()) {
        json["line"] = *finding.line;
    } else {
        json["line"] = nullptr;
    }
    json["evidence_commits"] = nlohmann::json::array();
    for (const auto& commit : finding.evidence_commits) {
        json["evidence_commits"].push_back(to_hex(commit.value));
    }
    return json;
}

nlohmann::json risk_vector_json(RiskVector risk) {
    return {
        {"structural", risk.structural},
        {"law_distance", risk.law_distance},
        {"repair_distance", risk.repair_distance},
        {"surprise", risk.surprise}
    };
}

nlohmann::json evidence_json(const RiskEvidence& evidence) {
    nlohmann::json json;
    json["component"] = evidence.component;
    json["input_root"] = to_hex(evidence.input_root);
    json["output_root"] = to_hex(evidence.output_root);
    json["objects"] = nlohmann::json::array();
    for (const auto object : evidence.objects) {
        json["objects"].push_back(to_string(object));
    }
    json["pointers"] = nlohmann::json::array();
    for (const auto pointer : evidence.pointers) {
        json["pointers"].push_back(to_string(pointer));
    }
    json["commits"] = nlohmann::json::array();
    for (const auto commit : evidence.commits) {
        json["commits"].push_back(to_hex(commit.value));
    }
    json["laws"] = evidence.laws;
    json["explanation"] = evidence.explanation;
    return json;
}

nlohmann::json measured_risk_json(const MeasuredRisk& measured) {
    nlohmann::json json;
    json["commit"] = to_hex(measured.commit.value);
    json["commit_root"] = to_hex(measured.commit_root);
    json["spec_hash"] = to_hex(measured.spec_hash);
    json["risk"] = risk_vector_json(measured.value);
    json["projection"] = measured.projection;
    json["evidence_root"] = to_hex(measured.evidence_root);
    json["measurement_object"] = to_hex(measured.measurement_object);
    json["measurement_hash"] = to_hex(measured.measurement_hash);
    json["evidence"] = nlohmann::json::array();
    for (const auto& evidence : measured.evidence) {
        json["evidence"].push_back(evidence_json(evidence));
    }
    return json;
}

nlohmann::json strict_policy_json(const GuardStrictPolicy& policy) {
    return {
        {"fail_on_law_distance", policy.fail_on_law_distance},
        {"fail_on_repair_distance", policy.fail_on_repair_distance},
        {"structural_percentile", policy.structural_percentile},
        {"surprise_percentile", policy.surprise_percentile},
        {"min_history_commits_for_calibration", policy.min_history_commits_for_calibration}
    };
}

nlohmann::json strict_decision_json(const GuardStrictDecision& decision) {
    return {
        {"failed", decision.failed},
        {"law_distance_failed", decision.law_distance_failed},
        {"repair_distance_failed", decision.repair_distance_failed},
        {"structural_failed", decision.structural_failed},
        {"surprise_failed", decision.surprise_failed},
        {"baseline_contaminated", decision.baseline_contaminated},
        {"structural_threshold", decision.structural_threshold},
        {"surprise_threshold", decision.surprise_threshold},
        {"calibration_commits", decision.calibration_commits},
        {"warnings", decision.warnings}
    };
}

std::string sarif_level(FindingSeverity severity) {
    switch (severity) {
    case FindingSeverity::Critical:
    case FindingSeverity::High:
        return "error";
    case FindingSeverity::Medium:
    case FindingSeverity::Low:
        return "warning";
    case FindingSeverity::Info:
        return "note";
    }
    return "note";
}

}  // namespace

int guard_risk_score(const std::vector<GuardFinding>& findings) noexcept {
    int score = 0;
    for (const auto& finding : findings) {
        switch (finding.severity) {
        case FindingSeverity::Info:
            break;
        case FindingSeverity::Low:
            score += 3;
            break;
        case FindingSeverity::Medium:
            score += 10;
            break;
        case FindingSeverity::High:
            score += 20;
            break;
        case FindingSeverity::Critical:
            score += 35;
            break;
        }
    }
    return std::min(score, 100);
}

std::string guard_status_for_risk(int risk_score) {
    if (risk_score >= 90) {
        return "critical";
    }
    if (risk_score >= 70) {
        return "risky";
    }
    if (risk_score >= 30) {
        return "attention";
    }
    return "clean";
}

std::string render_guard_report_text(const GuardReport& report) {
    std::ostringstream output;
    output << "Pointerverse Guard\n";
    output << "------------------\n";
    output << fmt::format(
        "measured risk: structural={} law={} repair={} surprise={}\n",
        report.measured_risk.structural,
        report.measured_risk.law_distance,
        report.measured_risk.repair_distance,
        report.measured_risk.surprise);
    output << fmt::format("projection: {}\n", report.projected_score);
    output << fmt::format("measurement spec: {}\n", to_hex(report.measurement_spec_hash).substr(0, 12));
    if (!empty(report.baseline_hash)) {
        output << fmt::format("baseline: {} {}\n", report.baseline, to_hex(report.baseline_hash).substr(0, 12));
    }
    output << fmt::format("status: {}\n", report.status);
    output << fmt::format("changed files: {}\n", report.changed_files);
    output << fmt::format("diff: +{} -{}\n", report.additions, report.deletions);

    output << "\nFindings:\n";
    if (report.findings.empty()) {
        output << "none\n";
    } else {
        for (const auto& finding : report.findings) {
            output << fmt::format("[{}] {}", to_string(finding.severity), finding.message);
            if (const auto file = display_file(finding); !file.empty()) {
                output << fmt::format(" ({})", file);
            }
            output << '\n';
        }
    }

    if (!report.strict_decision.warnings.empty()) {
        output << "\nCalibration:\n";
        for (const auto& warning : report.strict_decision.warnings) {
            output << warning << '\n';
        }
    }

    if (!report.artifacts.empty()) {
        output << "\nArtifacts:\n";
        for (const auto& artifact : report.artifacts) {
            output << artifact << '\n';
        }
    }
    return output.str();
}

std::string render_guard_report_markdown(const GuardReport& report) {
    std::ostringstream output;
    output << "## Pointerverse Guard\n\n";
    output << fmt::format(
        "Measured risk: **structural={} law={} repair={} surprise={}**\n",
        report.measured_risk.structural,
        report.measured_risk.law_distance,
        report.measured_risk.repair_distance,
        report.measured_risk.surprise);
    output << fmt::format("Projection: **{}**\n", report.projected_score);
    output << fmt::format("Measurement spec: `{}`\n", to_hex(report.measurement_spec_hash).substr(0, 12));
    if (!empty(report.baseline_hash)) {
        output << fmt::format("Baseline: **{}** `{}`\n", report.baseline, to_hex(report.baseline_hash).substr(0, 12));
    }
    output << fmt::format("Status: **{}**\n", report.status);
    output << fmt::format("Changed files: **{}**\n", report.changed_files);
    output << fmt::format("Diff: **+{} -{}**\n\n", report.additions, report.deletions);
    output << "### Findings\n\n";
    if (report.findings.empty()) {
        output << "No findings.\n";
    } else {
        for (const auto& finding : report.findings) {
            output << fmt::format("- **{}**: {}", upper_severity(finding.severity), finding.message);
            if (const auto file = display_file(finding); !file.empty()) {
                output << fmt::format(" (`{}`)", file);
            }
            if (!finding.rule.empty()) {
                output << fmt::format(" [`{}`]", finding.rule);
            }
            output << '\n';
        }
    }
    if (!report.strict_decision.warnings.empty()) {
        output << "\n### Calibration\n\n";
        for (const auto& warning : report.strict_decision.warnings) {
            output << fmt::format("- {}\n", warning);
        }
    }
    if (!report.artifacts.empty()) {
        output << "\n### Artifacts\n\n";
        for (const auto& artifact : report.artifacts) {
            output << display_artifact(artifact);
        }
    }
    return output.str();
}

std::string render_guard_report_json(const GuardReport& report) {
    nlohmann::json json;
    json["tool"] = "Pointerverse Guard";
    json["repo"] = report.repo;
    json["base"] = report.base;
    json["head"] = report.head;
    json["mode"] = report.mode;
    json["risk_score"] = report.risk_score;
    json["measurement_spec_hash"] = to_hex(report.measurement_spec_hash);
    json["baseline"] = report.baseline;
    json["baseline_hash"] = empty(report.baseline_hash) ? "" : to_hex(report.baseline_hash);
    json["measured_risk"] = risk_vector_json(report.measured_risk);
    json["projected_score"] = report.projected_score;
    json["strict_policy"] = strict_policy_json(report.strict_policy);
    json["strict_decision"] = strict_decision_json(report.strict_decision);
    json["status"] = report.status;
    json["changed_files"] = report.changed_files;
    json["additions"] = report.additions;
    json["deletions"] = report.deletions;
    json["ingested_events"] = report.ingested_events;
    json["ingestion_violations"] = report.ingestion_violations;
    json["affected_files"] = report.affected_files;
    json["artifacts"] = report.artifacts;
    json["evidence_commits"] = nlohmann::json::array();
    for (const auto& commit : report.evidence_commits) {
        json["evidence_commits"].push_back(to_hex(commit.value));
    }
    json["measured_risks"] = nlohmann::json::array();
    for (const auto& measured : report.measured_risks) {
        json["measured_risks"].push_back(measured_risk_json(measured));
    }
    json["findings"] = nlohmann::json::array();
    for (const auto& finding : report.findings) {
        json["findings"].push_back(finding_json(finding));
    }
    return json.dump(2) + "\n";
}

std::string render_guard_report_sarif(const GuardReport& report) {
    nlohmann::json rules = nlohmann::json::array();
    nlohmann::json results = nlohmann::json::array();
    std::vector<std::string> seen_rules;

    for (const auto& finding : report.findings) {
        if (std::ranges::find(seen_rules, finding.rule) == seen_rules.end()) {
            seen_rules.push_back(finding.rule);
            rules.push_back({
                {"id", finding.rule},
                {"name", finding.rule},
                {"shortDescription", {{"text", finding.rule}}}
            });
        }

        nlohmann::json location;
        if (!finding.file.empty()) {
            location["physicalLocation"]["artifactLocation"]["uri"] = finding.file;
            if (finding.line.has_value()) {
                location["physicalLocation"]["region"]["startLine"] = *finding.line;
            }
        }

        nlohmann::json result = {
            {"ruleId", finding.rule},
            {"level", sarif_level(finding.severity)},
            {"message", {{"text", finding.message}}}
        };
        if (!location.empty()) {
            result["locations"] = nlohmann::json::array({location});
        }
        results.push_back(std::move(result));
    }

    const nlohmann::json sarif = {
        {"version", "2.1.0"},
        {"$schema", "https://json.schemastore.org/sarif-2.1.0.json"},
        {"runs", nlohmann::json::array({
            {
                {"tool", {
                    {"driver", {
                        {"name", "Pointerverse Guard"},
                        {"informationUri", "https://github.com/farukalpay/Pointerverse"},
                        {"rules", rules}
                    }}
                }},
                {"results", results}
            }
        })}
    };
    return sarif.dump(2) + "\n";
}

std::string render_guard_report(const GuardReport& report, std::string_view format) {
    if (format == "text") {
        return render_guard_report_text(report);
    }
    if (format == "json") {
        return render_guard_report_json(report);
    }
    if (format == "markdown") {
        return render_guard_report_markdown(report);
    }
    if (format == "sarif") {
        return render_guard_report_sarif(report);
    }
    throw std::invalid_argument("format must be text, json, markdown, or sarif");
}

}  // namespace pv
