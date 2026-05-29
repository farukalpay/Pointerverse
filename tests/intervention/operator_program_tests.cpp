// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "pv/intervention/operator_program.hpp"

using namespace pv;

namespace {

RepairCandidate candidate(RepairAction action) {
    RepairCandidate out;
    out.breakpoint_id = "bp";
    out.branch = "main";
    out.action = action;
    out.trigger.from = "A";
    out.trigger.to = "B";
    out.trigger.relation = "causes";
    out.evidence_ids = {"external/e1"};
    out.script = "unlink A -> B : causes pointer=P1\n";
    return out;
}

}  // namespace

TEST_CASE("intervention programs have stable identity cost and hash") {
    const auto op = intervention_operator_from_repair(
        candidate(RepairAction::RemoveTriggeringRelation),
        ScaleValue::dyadic(1, 1));

    const auto first = make_intervention_program({op});
    const auto second = make_intervention_program({op});

    REQUIRE(first == second);
    REQUIRE(first.canonical_cost == op.canonical_cost);
    REQUIRE(first.canonical_cost > 0);
    REQUIRE(intervention_program_id(first).size() == 12);
    REQUIRE(identity_intervention_program().operators.empty());
}
