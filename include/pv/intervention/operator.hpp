// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pv/breakpoint/repair_candidate.hpp"
#include "pv/hash/canonical.hpp"
#include "pv/intervention/scale_value.hpp"

namespace pv {

class CanonicalWriter;

enum class InterventionKind {
    Identity,
    ConstrainTriggeringRelation,
    DelayTriggeringRelation,
    ReplaceTriggeringRelation,
    RemoveTriggeringRelation
};

struct InterventionOperator {
    std::string id;
    std::string breakpoint_id;
    std::string branch;
    std::string family;
    InterventionKind kind{InterventionKind::Identity};
    ScaleValue scale{ScaleValue::zero()};
    std::vector<std::string> target_evidence_ids;
    std::vector<std::string> target_relation_ids;
    std::vector<std::string> target_entity_ids;
    BreakpointTrigger trigger;
    PointerId pointer;
    std::optional<double> replacement_weight;
    std::string replacement_relation;
    std::string script;
    std::uint64_t canonical_cost{0};
    Hash256 canonical_hash;

    friend bool operator==(const InterventionOperator&, const InterventionOperator&) = default;
};

[[nodiscard]] std::string_view to_string(InterventionKind kind) noexcept;
[[nodiscard]] InterventionKind intervention_kind_for_repair(RepairAction action) noexcept;
[[nodiscard]] RepairAction repair_action_for_intervention(InterventionKind kind);
[[nodiscard]] Hash256 intervention_operator_hash(const InterventionOperator& op);
[[nodiscard]] InterventionOperator canonicalize_intervention_operator(InterventionOperator op);
[[nodiscard]] InterventionOperator intervention_operator_from_repair(
    const RepairCandidate& candidate,
    ScaleValue scale);
[[nodiscard]] RepairCandidate repair_candidate_from_operator(const InterventionOperator& op);

void encode(CanonicalWriter& writer, const InterventionOperator& op);

}  // namespace pv
