// SPDX-License-Identifier: Apache-2.0
#include "pv/audit/report.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <utility>

#include "pv/hash/canonical.hpp"
#include "pv/law/law.hpp"
#include "pv/measure/measurement_store.hpp"
#include "pv/measure/risk_projection.hpp"
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
    json["projection_policy_hash"] = to_hex(measured.projection_result.projection_policy_hash);
    json["projection_hash"] = to_hex(measured.projection_result.projection_hash);
    json["evidence_root"] = to_hex(measured.evidence_root);
    json["measurement_object"] = to_hex(measured.measurement_object);
    json["measurement_hash"] = to_hex(measured.measurement_hash);
    json["evidence"] = nlohmann::json::array();
    for (const auto& evidence : measured.evidence) {
        json["evidence"].push_back(evidence_json(evidence));
    }
    return json;
}

}  // namespace

AuditReport AuditReportGenerator::generate(
    Repository& repository,
    std::string_view branch) const {
    AuditReport report;
    report.branch = std::string{branch};
    const auto spec = default_measurement_spec();
    report.measurement_spec_hash = measurement_spec_hash(spec);
    auto measured = MeasurementStore{repository}.measure_or_load_branch(branch, spec);
    report.measured_risks = std::move(measured.measured);
    report.risk = joined_risk(report.measured_risks);
    const auto projection = make_projection_result(Hash256{}, report.risk, spec.projection);
    report.projection_policy_hash = projection.projection_policy_hash;
    report.projection_hash = projection.projection_hash;
    report.projected_score = projection.projected_score;
    report.risk_score = static_cast<int>(std::min<std::uint64_t>(report.projected_score, 100));

    for (const auto& record : repository.history(branch)) {
        if (record.origin == TransactionOrigin::Internal) {
            continue;
        }
        report.commits_checked += 1;
        const auto objects = touched_objects(record);
        for (const auto& violation : record.violations) {
            const auto severity = to_string(violation.severity);
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
    output << fmt::format(
        "risk vector: structural={} law={} repair={} surprise={}\n",
        report.risk.structural,
        report.risk.law_distance,
        report.risk.repair_distance,
        report.risk.surprise);
    output << fmt::format("projection: {}\n", report.projected_score);
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
    json["measurement_spec_hash"] = to_hex(report.measurement_spec_hash);
    json["projection_policy_hash"] = to_hex(report.projection_policy_hash);
    json["risk"] = risk_vector_json(report.risk);
    json["projected_score"] = report.projected_score;
    json["projection_hash"] = to_hex(report.projection_hash);
    json["risk_score"] = report.risk_score;
    json["measured_risks"] = nlohmann::json::array();
    for (const auto& measured : report.measured_risks) {
        json["measured_risks"].push_back(measured_risk_json(measured));
    }
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
