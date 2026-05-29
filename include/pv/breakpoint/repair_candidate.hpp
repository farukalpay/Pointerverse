// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>

#include "pv/breakpoint/breakpoint.hpp"

namespace pv {

class ProjectionStore;

enum class RepairAction {
    RemoveTriggeringRelation,
    ConstrainTriggeringRelation,
    DelayTriggeringRelation,
    ReplaceTriggeringRelation
};

struct RepairCandidate {
    std::string breakpoint_id;
    std::string branch;
    RepairAction action{RepairAction::RemoveTriggeringRelation};
    BreakpointTrigger trigger;
    PointerId pointer;
    std::vector<std::string> evidence_ids;
    std::optional<double> replacement_weight;
    std::string replacement_relation;
    std::string script;
};

class RepairCandidateBuilder {
public:
    [[nodiscard]] std::vector<RepairCandidate> build_all(
        const ProjectionStore& store,
        std::string_view branch,
        const Breakpoint& breakpoint) const;

    [[nodiscard]] RepairCandidate build(
        const ProjectionStore& store,
        std::string_view branch,
        const Breakpoint& breakpoint) const;
};

[[nodiscard]] std::string_view to_string(RepairAction action) noexcept;

}  // namespace pv
