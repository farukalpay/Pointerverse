// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "pv/decision/recommendation.hpp"
#include "pv/decision/signal.hpp"

namespace pv {

struct DecisionReport {
    std::string branch;
    std::vector<Signal> signals;
    std::vector<Recommendation> recommendations;

    friend bool operator==(const DecisionReport&, const DecisionReport&) = default;
};

[[nodiscard]] std::string render_decision_report_text(const DecisionReport& report);

}  // namespace pv
