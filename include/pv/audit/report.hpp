// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pv/runtime/ids.hpp"
#include "pv/core/id.hpp"
#include "pv/measure/risk_functional.hpp"

namespace pv {

class Repository;

struct AuditViolation {
    CommitId commit;
    Epoch epoch;
    std::string law;
    std::string severity;
    std::string explanation;
    std::vector<std::string> objects;
};

struct AuditReport {
    std::string branch;
    std::size_t commits_checked{0};
    Hash256 measurement_spec_hash;
    RiskVector risk;
    std::uint64_t projected_score{0};
    std::vector<MeasuredRisk> measured_risks;
    int risk_score{0};
    std::vector<AuditViolation> violations;
    std::vector<std::string> warnings;
};

class AuditReportGenerator {
public:
    [[nodiscard]] AuditReport generate(
        Repository& repository,
        std::string_view branch) const;
};

[[nodiscard]] std::string render_audit_report_text(const AuditReport& report);
[[nodiscard]] std::string render_audit_report_json(const AuditReport& report);
[[nodiscard]] std::string render_audit_violations_text(const AuditReport& report);

// Earliest commit on the branch that recorded a violation of the named law,
// or nullopt if the law never broke. "Earliest" is the smallest epoch, which
// is order-independent because each commit advances the epoch.
[[nodiscard]] std::optional<AuditViolation> first_violation(const AuditReport& report, std::string_view law);
[[nodiscard]] std::string render_first_break_text(
    std::string_view branch,
    std::string_view law,
    const std::optional<AuditViolation>& violation);

}  // namespace pv
