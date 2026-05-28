// SPDX-License-Identifier: Apache-2.0
#include "pv/guard/report.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "pv/hash/canonical.hpp"

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
        score += risk_points(finding.severity);
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
    output << fmt::format("risk score: {} / 100\n", report.risk_score);
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
    output << fmt::format("Risk score: **{} / 100**\n", report.risk_score);
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
