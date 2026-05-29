// SPDX-License-Identifier: Apache-2.0
#include "pv/intervention/intervention_lattice.hpp"

#include <algorithm>
#include <utility>

namespace pv {
namespace {

bool same_operator_family_shape(const InterventionProgram& left, const InterventionProgram& right) {
    if (left.operators.size() != right.operators.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.operators.size(); ++index) {
        if (left.operators[index].family != right.operators[index].family
            || left.operators[index].kind != right.operators[index].kind
            || left.operators[index].target_evidence_ids != right.operators[index].target_evidence_ids
            || left.operators[index].target_relation_ids != right.operators[index].target_relation_ids
            || left.operators[index].target_entity_ids != right.operators[index].target_entity_ids) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::string_view to_string(InterventionOrder order) noexcept {
    switch (order) {
    case InterventionOrder::Weaker:
        return "weaker";
    case InterventionOrder::Stronger:
        return "stronger";
    case InterventionOrder::Equivalent:
        return "equivalent";
    case InterventionOrder::Incomparable:
        return "incomparable";
    }
    return "incomparable";
}

InterventionOrder compare_interventions(
    const InterventionProgram& left,
    const InterventionProgram& right) noexcept {
    if (left.canonical_hash == right.canonical_hash) {
        return InterventionOrder::Equivalent;
    }
    if (!same_operator_family_shape(left, right)) {
        return InterventionOrder::Incomparable;
    }
    bool left_weaker = false;
    bool left_stronger = false;
    for (std::size_t index = 0; index < left.operators.size(); ++index) {
        if (left.operators[index].scale < right.operators[index].scale) {
            left_weaker = true;
        }
        if (left.operators[index].scale > right.operators[index].scale) {
            left_stronger = true;
        }
    }
    if (left_weaker && !left_stronger) {
        return InterventionOrder::Weaker;
    }
    if (left_stronger && !left_weaker) {
        return InterventionOrder::Stronger;
    }
    return InterventionOrder::Incomparable;
}

InterventionLatticeRelation intervention_lattice_relation(
    InterventionProgram left,
    InterventionProgram right) {
    InterventionLatticeRelation relation;
    relation.left = std::move(left);
    relation.right = std::move(right);
    relation.order = compare_interventions(relation.left, relation.right);
    relation.explanation = std::string{"left is "} + std::string{to_string(relation.order)} + " relative to right";
    return relation;
}

}  // namespace pv
