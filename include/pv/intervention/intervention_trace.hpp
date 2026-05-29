// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "pv/breakpoint/breakpoint.hpp"
#include "pv/hash/canonical.hpp"
#include "pv/intervention/operator_program.hpp"
#include "pv/law/law.hpp"
#include "pv/runtime/ids.hpp"

namespace pv {

struct InterventionTraceStep {
    CommitId source_commit;
    CommitId replay_commit;
    Hash256 delta_hash;
    bool transformed{false};
    std::string label;
    std::vector<LawStatus> law_statuses;
    std::vector<LawViolation> violations;
};

struct InterventionTrace {
    Hash256 search_id;
    InterventionProgram program;
    bool replayed{false};
    bool transformed{false};
    bool survives{true};
    std::optional<Breakpoint> survivor;
    std::vector<Breakpoint> breakpoints;
    std::vector<InterventionTraceStep> steps;
    std::vector<LawStatus> law_statuses;
    std::vector<LawViolation> violations;
    std::vector<std::string> evidence_ids;
    std::string explanation;
};

[[nodiscard]] std::string render_intervention_trace_text(const InterventionTrace& trace);

}  // namespace pv
