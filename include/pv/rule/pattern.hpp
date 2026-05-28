// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "pv/core/delta.hpp"
#include "pv/core/snapshot.hpp"
#include "pv/law/law.hpp"

namespace pv {

enum class PatternEndpointBinding {
    Any,
    TriggerFrom,
    TriggerTo
};

enum class RequirementSearch {
    Before,
    After,
    BeforeOrAfter
};

struct RelationPattern {
    std::string from_type;
    std::string relation;
    std::string to_type;
};

struct RequirementPattern {
    RelationPattern pattern;
    PatternEndpointBinding from_binding{PatternEndpointBinding::TriggerFrom};
    PatternEndpointBinding to_binding{PatternEndpointBinding::TriggerTo};
    RequirementSearch search{RequirementSearch::BeforeOrAfter};
};

struct PatternMatch {
    ObjectId from;
    ObjectId to;
    std::string from_name;
    std::string to_name;
    std::string relation;
};

[[nodiscard]] bool matches_relation_pattern(
    const WorldSnapshot& snapshot,
    const RelationPattern& pattern,
    ObjectId from,
    ObjectId to,
    RelationType relation);

[[nodiscard]] std::vector<PatternMatch> trigger_matches(
    const LawCheckContext& ctx,
    const RelationPattern& trigger);

[[nodiscard]] bool requirement_exists(
    const LawCheckContext& ctx,
    const PatternMatch& trigger,
    const RequirementPattern& requirement);

}  // namespace pv
