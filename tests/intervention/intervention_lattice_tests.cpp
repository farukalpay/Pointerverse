// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "pv/intervention/intervention_lattice.hpp"

using namespace pv;

namespace {

InterventionProgram program(RepairAction action, ScaleValue scale) {
    RepairCandidate seed;
    seed.breakpoint_id = "bp";
    seed.branch = "main";
    seed.action = action;
    seed.trigger.from = "A";
    seed.trigger.to = "B";
    seed.trigger.relation = "causes";
    seed.evidence_ids = {"external/e1"};
    seed.script = "unlink A -> B : causes pointer=P1\n";
    return make_intervention_program({intervention_operator_from_repair(seed, scale)});
}

}  // namespace

TEST_CASE("intervention lattice compares strength by shared family scale") {
    const auto weaker = program(RepairAction::RemoveTriggeringRelation, ScaleValue::dyadic(1, 2));
    const auto stronger = program(RepairAction::RemoveTriggeringRelation, ScaleValue::one());
    const auto other = program(RepairAction::DelayTriggeringRelation, ScaleValue::one());

    REQUIRE(compare_interventions(weaker, stronger) == InterventionOrder::Weaker);
    REQUIRE(compare_interventions(stronger, weaker) == InterventionOrder::Stronger);
    REQUIRE(compare_interventions(stronger, stronger) == InterventionOrder::Equivalent);
    REQUIRE(compare_interventions(stronger, other) == InterventionOrder::Incomparable);
}
