// SPDX-License-Identifier: Apache-2.0
#include "pv/intervention/intervention_trace.hpp"

#include <sstream>

#include <fmt/format.h>

namespace pv {

std::string render_intervention_trace_text(const InterventionTrace& trace) {
    std::ostringstream output;
    output << fmt::format("Intervention trace: {}\n", to_hex(trace.search_id).substr(0, 12));
    output << "-------------------\n";
    output << fmt::format("program:     {}\n", intervention_program_id(trace.program));
    output << fmt::format("operators:   {}\n", trace.program.operators.size());
    output << fmt::format("replayed:    {}\n", trace.replayed ? "yes" : "no");
    output << fmt::format("transformed: {}\n", trace.transformed ? "yes" : "no");
    output << fmt::format("survives:    {}\n", trace.survives ? "yes" : "no");
    output << fmt::format("evidence:    {}\n", trace.evidence_ids.size());
    output << fmt::format("explanation: {}\n", trace.explanation);
    output << "steps:\n";
    for (const auto& step : trace.steps) {
        output << fmt::format(
            "  source={} replay={} delta={} transformed={} laws={} violations={} {}\n",
            to_hex(step.source_commit.value).substr(0, 12),
            to_hex(step.replay_commit.value).substr(0, 12),
            to_hex(step.delta_hash).substr(0, 12),
            step.transformed ? "yes" : "no",
            step.law_statuses.size(),
            step.violations.size(),
            step.label);
    }
    return output.str();
}

}  // namespace pv
