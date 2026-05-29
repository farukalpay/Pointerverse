// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pv/breakpoint/breakpoint.hpp"

namespace pv {

class ProjectionStore;

struct BreakpointFindOptions {
    std::size_t repeated_relation_threshold{2};
    std::size_t concentration_min_events{4};
    double concentration_share{0.60};
    bool include_branch_divergence{true};
};

class BreakpointFinder {
public:
    [[nodiscard]] std::vector<Breakpoint> find(
        const ProjectionStore& store,
        std::string_view branch,
        BreakpointFindOptions options = {}) const;

    [[nodiscard]] std::optional<Breakpoint> find_by_id(
        const ProjectionStore& store,
        std::string_view branch,
        std::string_view breakpoint_id,
        BreakpointFindOptions options = {}) const;
};

[[nodiscard]] std::string render_breakpoints_text(
    std::string_view branch,
    const std::vector<Breakpoint>& breakpoints);

}  // namespace pv
