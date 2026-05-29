// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "pv/decision/signal_model.hpp"

using namespace pv;

TEST_CASE("signal model emits generic evidence-backed signals") {
    EntityProjectionEntry entity;
    entity.entity = "A";
    entity.appearances = 3;
    entity.evidence_event_ids = {"external/e1"};

    RelationProjectionEntry relation;
    relation.relation = "causes";
    relation.occurrences = 2;
    relation.evidence_event_ids = {"external/e1", "external/e2"};

    const SignalModel model;
    const auto signals = model.signals({entity}, {relation});
    const auto recommendations = model.recommendations(signals);

    REQUIRE(signals.size() == 2);
    REQUIRE(recommendations.size() == 2);
}

TEST_CASE("signal model does not recommend without evidence ids") {
    Signal signal;
    signal.id = "signal/no-evidence";
    signal.entity = "A";
    signal.kind = "high_activity_entity";
    signal.score = 10.0;

    const SignalModel model;
    REQUIRE(model.recommendations({signal}).empty());

    EntityProjectionEntry entity;
    entity.entity = "A";
    entity.appearances = 10;
    REQUIRE(model.signals({entity}, {}).empty());
}
