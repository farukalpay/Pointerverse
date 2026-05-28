// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "pv/runtime/ids.hpp"
#include "pv/core/id.hpp"

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
    int risk_score{0};
    std::vector<AuditViolation> violations;
    std::vector<std::string> warnings;
};

class AuditReportGenerator {
public:
    [[nodiscard]] AuditReport generate(
        const Repository& repository,
        std::string_view branch) const;
};

[[nodiscard]] std::string render_audit_report_text(const AuditReport& report);
[[nodiscard]] std::string render_audit_report_json(const AuditReport& report);
[[nodiscard]] std::string render_audit_violations_text(const AuditReport& report);

}  // namespace pv
