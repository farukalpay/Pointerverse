// SPDX-License-Identifier: Apache-2.0
#include "pv/audit/violation_report.hpp"

#include <fmt/format.h>

#include <sstream>

#include "pv/hash/canonical.hpp"

namespace pv {
namespace {

std::string short_hash(CommitId id) {
    return to_hex(id.value).substr(0, 12);
}

}  // namespace

std::string render_audit_violations_text(const AuditReport& report) {
    std::ostringstream output;
    output << fmt::format("Audit violations: {}\n", report.branch);
    output << "-----------------\n";
    if (report.violations.empty()) {
        output << "no violations\n";
        return output.str();
    }
    for (const auto& violation : report.violations) {
        output << fmt::format(
            "[{}] {} epoch {} commit {}\n",
            violation.severity,
            violation.law,
            violation.epoch.value,
            short_hash(violation.commit));
        output << fmt::format("  {}\n", violation.explanation);
    }
    return output.str();
}

}  // namespace pv
