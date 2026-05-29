// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "pv/breakpoint/breakpoint.hpp"
#include "pv/breakpoint/breakpoint_finder.hpp"
#include "pv/breakpoint/repair_candidate.hpp"
#include "pv/law/verifier.hpp"
#include "pv/measure/counterfactual_measure.hpp"

namespace pv {

class ProjectionStore;
class Repository;

struct BreakpointMeasurement {
    Breakpoint breakpoint;
    std::uint64_t severity{std::numeric_limits<std::uint64_t>::max()};
    double compression{0.0};
    bool has_eliminating_repair{false};
    std::vector<std::string> evidence_chain_ids;
    std::vector<CounterfactualRepairMeasure> repairs;
};

class BreakpointMeasure {
public:
    [[nodiscard]] BreakpointMeasurement measure(
        const Repository& repository,
        const ProjectionStore& store,
        std::string_view branch,
        const Breakpoint& breakpoint,
        const Verifier* verifier = nullptr,
        BreakpointFindOptions find_options = {}) const;

    [[nodiscard]] std::vector<BreakpointMeasurement> rank(
        const Repository& repository,
        const ProjectionStore& store,
        std::string_view branch,
        const Verifier* verifier = nullptr,
        BreakpointFindOptions find_options = {}) const;
};

[[nodiscard]] std::uint64_t edge_criticality(
    std::string_view evidence_edge,
    const std::vector<BreakpointMeasurement>& measurements);

[[nodiscard]] std::string render_breakpoint_measure_text(const BreakpointMeasurement& measurement);
[[nodiscard]] std::string render_breakpoint_rank_text(
    std::string_view branch,
    const std::vector<BreakpointMeasurement>& measurements);
[[nodiscard]] std::string render_repair_set_text(const BreakpointMeasurement& measurement);

}  // namespace pv
