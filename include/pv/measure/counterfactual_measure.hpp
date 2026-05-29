// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pv/breakpoint/breakpoint.hpp"
#include "pv/breakpoint/breakpoint_finder.hpp"
#include "pv/breakpoint/repair_candidate.hpp"
#include "pv/law/verifier.hpp"

namespace pv {

class ProjectionStore;
class Repository;

struct CounterfactualRepairMeasure {
    RepairCandidate candidate;
    std::uint64_t canonical_cost{0};
    std::string canonical_script;
    bool replayed{false};
    bool survives{true};
    std::optional<Breakpoint> survivor;
    std::string explanation;
};

class CounterfactualMeasure {
public:
    [[nodiscard]] CounterfactualRepairMeasure evaluate(
        const Repository& repository,
        std::string_view branch,
        const Breakpoint& breakpoint,
        const RepairCandidate& candidate,
        const Verifier* verifier = nullptr,
        BreakpointFindOptions find_options = {}) const;
};

[[nodiscard]] bool equivalent_breakpoint(
    const Breakpoint& original,
    const Breakpoint& candidate);

[[nodiscard]] std::optional<Breakpoint> surviving_breakpoint(
    const Breakpoint& original,
    const std::vector<Breakpoint>& repaired);

}  // namespace pv
