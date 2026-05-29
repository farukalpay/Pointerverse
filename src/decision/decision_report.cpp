// SPDX-License-Identifier: Apache-2.0
#include "pv/decision/decision_report.hpp"

#include <fmt/format.h>

#include <sstream>

namespace pv {

std::string render_decision_report_text(const DecisionReport& report) {
    std::ostringstream output;
    output << fmt::format("Decision report: {}\n", report.branch);
    output << "----------------\n";
    if (report.recommendations.empty()) {
        output << "No evidence-backed recommendations.\n";
        return output.str();
    }

    output << "High priority:\n";
    std::size_t index = 1;
    for (const auto& recommendation : report.recommendations) {
        if (recommendation.priority != "high" && recommendation.priority != "critical") {
            continue;
        }
        output << fmt::format("{}. {}\n", index++, recommendation.reason);
    }
    if (index == 1) {
        output << "none\n";
    }

    output << "\nRecommended actions:\n";
    for (const auto& recommendation : report.recommendations) {
        output << fmt::format("- {}\n", recommendation.action);
    }
    return output.str();
}

}  // namespace pv
