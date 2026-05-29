// SPDX-License-Identifier: Apache-2.0
#include "pv/audit/report.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>

#include "pv/audit/risk_score.hpp"
#include "pv/hash/canonical.hpp"
#include "pv/law/law.hpp"
#include "pv/runtime/transaction.hpp"
#include "pv/storage/repository.hpp"

namespace pv {
namespace {

std::string short_hash(CommitId id) {
    return to_hex(id.value).substr(0, 12);
}

std::vector<std::string> touched_objects(const CommitRecord& record) {
    std::set<std::string> names;
    for (const auto& event : record.events) {
        for (const auto* key : {"object", "from", "to", "actor"}) {
            if (const auto iter = event.fields.find(key); iter != event.fields.end() && !iter->second.empty()) {
                names.insert(iter->second);
            }
        }
    }
    return {names.begin(), names.end()};
}

}  // namespace

AuditReport AuditReportGenerator::generate(
    const Repository& repository,
    std::string_view branch) const {
    AuditReport report;
    report.branch = std::string{branch};

    for (const auto& record : repository.history(branch)) {
        if (record.origin == TransactionOrigin::Internal) {
            continue;
        }
        report.commits_checked += 1;
        const auto objects = touched_objects(record);
        for (const auto& violation : record.violations) {
            const auto severity = to_string(violation.severity);
            report.risk_score += risk_points(violation.severity);
            report.violations.push_back(AuditViolation{
                record.id,
                record.after_epoch,
                violation.law,
                severity,
                violation.explanation,
                objects
            });
        }
        if (!record.accepted) {
            report.warnings.push_back(fmt::format(
                "commit {} was rejected and did not advance the branch",
                short_hash(record.id)));
        }
    }

    return report;
}

std::string render_audit_report_text(const AuditReport& report) {
    std::ostringstream output;
    output << fmt::format("Audit report: {}\n", report.branch);
    output << "------------------\n";
    output << fmt::format("commits checked: {}\n", report.commits_checked);
    output << fmt::format("violations: {}\n", report.violations.size());
    output << fmt::format("risk score: {}\n", report.risk_score);
    if (report.violations.empty()) {
        output << "\nno violations\n";
        return output.str();
    }

    for (const auto& violation : report.violations) {
        output << '\n';
        output << fmt::format("[{}] {}\n", violation.severity, violation.law);
        output << fmt::format("epoch {} commit {}\n", violation.epoch.value, short_hash(violation.commit));
        output << violation.explanation << '\n';
        if (!violation.objects.empty()) {
            output << "objects:";
            for (const auto& object : violation.objects) {
                output << ' ' << object;
            }
            output << '\n';
        }
    }
    return output.str();
}

std::optional<AuditViolation> first_violation(const AuditReport& report, std::string_view law) {
    const AuditViolation* earliest = nullptr;
    for (const auto& violation : report.violations) {
        if (violation.law != law) {
            continue;
        }
        if (earliest == nullptr || violation.epoch.value < earliest->epoch.value) {
            earliest = &violation;
        }
    }
    if (earliest == nullptr) {
        return std::nullopt;
    }
    return *earliest;
}

std::string render_first_break_text(
    std::string_view branch,
    std::string_view law,
    const std::optional<AuditViolation>& violation) {
    std::ostringstream output;
    output << fmt::format("first-broke: law {} on branch {}\n", law, branch);
    if (!violation.has_value()) {
        output << "  never broke on this branch\n";
        return output.str();
    }
    output << fmt::format(
        "  first broke at epoch {} commit {}\n", violation->epoch.value, short_hash(violation->commit));
    output << fmt::format("  severity: {}\n", violation->severity);
    output << fmt::format("  {}\n", violation->explanation);
    if (!violation->objects.empty()) {
        output << "  objects:";
        for (const auto& object : violation->objects) {
            output << ' ' << object;
        }
        output << '\n';
    }
    return output.str();
}

std::string render_audit_report_json(const AuditReport& report) {
    nlohmann::json json;
    json["branch"] = report.branch;
    json["commits_checked"] = report.commits_checked;
    json["risk_score"] = report.risk_score;
    json["warnings"] = report.warnings;
    json["violations"] = nlohmann::json::array();
    for (const auto& violation : report.violations) {
        json["violations"].push_back({
            {"commit", to_hex(violation.commit.value)},
            {"epoch", violation.epoch.value},
            {"law", violation.law},
            {"severity", violation.severity},
            {"explanation", violation.explanation},
            {"objects", violation.objects}
        });
    }
    return json.dump(2) + "\n";
}

}  // namespace pv
