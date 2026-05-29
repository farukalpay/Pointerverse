// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "pv/core/id.hpp"
#include "pv/core/pointer.hpp"
#include "pv/runtime/ids.hpp"

namespace pv {

enum class BreakpointKind {
    InvariantViolation,
    RepeatedRelation,
    AbnormalConcentration,
    BranchDivergence
};

struct BreakpointTrigger {
    CommitId commit;
    Epoch epoch;
    std::string event;
    std::string detail;
    std::string evidence_event_id;
    std::string from;
    std::string to;
    std::string relation;
    PointerId pointer;

    friend bool operator==(const BreakpointTrigger&, const BreakpointTrigger&) = default;
};

struct Breakpoint {
    std::string id;
    BreakpointKind kind{BreakpointKind::RepeatedRelation};
    std::string branch;
    std::string summary;
    BreakpointTrigger trigger;
    std::vector<std::string> affected_entities;
    std::vector<std::string> affected_relations;
    std::vector<PointerId> affected_pointers;
    std::vector<std::string> evidence_ids;

    friend bool operator==(const Breakpoint&, const Breakpoint&) = default;
};

[[nodiscard]] std::string_view to_string(BreakpointKind kind) noexcept;
[[nodiscard]] std::string make_breakpoint_id(const Breakpoint& breakpoint);

}  // namespace pv
