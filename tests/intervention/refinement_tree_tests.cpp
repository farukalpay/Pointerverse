// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "pv/intervention/refinement_tree.hpp"

using namespace pv;

namespace {

OperatorFamily family() {
    RepairCandidate seed;
    seed.breakpoint_id = "bp";
    seed.branch = "main";
    seed.action = RepairAction::ConstrainTriggeringRelation;
    seed.trigger.from = "A";
    seed.trigger.to = "B";
    seed.trigger.relation = "causes";
    seed.evidence_ids = {"external/e1"};
    seed.script = "constrain A -> B : causes weight=0.5 pointer=P1\n";

    OperatorFamily out;
    out.id = "constrain_triggering_relation";
    out.name = out.id;
    out.kind = InterventionKind::ConstrainTriggeringRelation;
    out.seed = std::move(seed);
    return canonicalize_operator_family(std::move(out));
}

}  // namespace

TEST_CASE("refinement tree repeats dyadic operator scales") {
    const auto first = refine_family(family(), 2);
    const auto second = refine_family(family(), 2);

    REQUIRE(first.size() == 4);
    REQUIRE(first.size() == second.size());
    REQUIRE(first.front().scale == ScaleValue::dyadic(1, 2));
    REQUIRE(first.back().scale == ScaleValue::one());
    REQUIRE(first.front().canonical_hash == second.front().canonical_hash);
}
