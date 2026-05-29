// SPDX-License-Identifier: Apache-2.0
#include "pv/intervention/operator_family.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

#include <fmt/format.h>

#include "pv/breakpoint/repair_candidate.hpp"
#include "pv/hash/hasher.hpp"
#include "pv/kernel/canonical_codec.hpp"

namespace pv {

Hash256 operator_family_hash(const OperatorFamily& family) {
    CanonicalWriter writer;
    writer.string("OperatorFamily:v1");
    writer.string(family.id);
    writer.string(family.name);
    writer.string(std::string{to_string(family.kind)});
    writer.string(family.seed.breakpoint_id);
    writer.string(family.seed.branch);
    writer.string(family.seed.trigger.from);
    writer.string(family.seed.trigger.to);
    writer.string(family.seed.trigger.relation);
    writer.hash(family.seed.trigger.commit.value);
    writer.u64(family.seed.pointer.value);
    writer.string(family.seed.script);
    return sha256(writer.bytes());
}

OperatorFamily canonicalize_operator_family(OperatorFamily family) {
    family.canonical_hash = operator_family_hash(family);
    if (family.id.empty()) {
        family.id = to_hex(family.canonical_hash).substr(0, 12);
        family.canonical_hash = operator_family_hash(family);
    }
    return family;
}

InterventionOperator make_operator(const OperatorFamily& family, ScaleValue scale) {
    auto candidate = family.seed;
    if (candidate.action == RepairAction::ConstrainTriggeringRelation
        && candidate.replacement_weight.has_value()) {
        candidate.replacement_weight = std::clamp(
            *candidate.replacement_weight * scale.to_double(),
            0.0,
            1.0);
        std::ostringstream script;
        script << "# pointerverse repair candidate v1\n";
        script << fmt::format("# breakpoint {}\n", candidate.breakpoint_id);
        for (const auto& evidence : candidate.evidence_ids) {
            script << fmt::format("# evidence {}\n", evidence);
        }
        script << fmt::format(
            "constrain {} -> {} : {} weight={:.12g} pointer=P{}\n",
            candidate.trigger.from,
            candidate.trigger.to,
            candidate.trigger.relation,
            *candidate.replacement_weight,
            candidate.pointer.value);
        candidate.script = script.str();
    }
    return intervention_operator_from_repair(candidate, scale);
}

std::vector<OperatorFamily> OperatorFamilyBuilder::build(
    const ProjectionStore& store,
    std::string_view branch,
    const Breakpoint& breakpoint) const {
    auto candidates = RepairCandidateBuilder{}.build_all(store, branch, breakpoint);
    std::vector<OperatorFamily> families;
    families.reserve(candidates.size());
    for (auto& candidate : candidates) {
        OperatorFamily family;
        family.name = std::string{to_string(intervention_kind_for_repair(candidate.action))};
        family.kind = intervention_kind_for_repair(candidate.action);
        family.seed = std::move(candidate);
        family.id = family.name;
        families.push_back(canonicalize_operator_family(std::move(family)));
    }
    std::ranges::sort(families, [](const auto& left, const auto& right) {
        return left.id < right.id;
    });
    return families;
}

}  // namespace pv
