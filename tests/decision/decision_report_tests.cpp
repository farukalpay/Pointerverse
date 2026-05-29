// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "pv/decision/decision_report.hpp"

using namespace pv;

TEST_CASE("decision report renders empty evidence-backed state") {
    DecisionReport report;
    report.branch = "main";

    const auto text = render_decision_report_text(report);
    REQUIRE(text.find("Decision report: main") != std::string::npos);
    REQUIRE(text.find("No evidence-backed recommendations.") != std::string::npos);
}

TEST_CASE("decision report renders recommendations and actions") {
    Signal signal;
    signal.id = "signal/relation/causes";
    signal.entity = "causes";
    signal.kind = "repeated_relation";
    signal.score = 4.0;
    signal.explanation = "causes repeats 4 times";
    signal.evidence_event_ids = {"external/e1"};

    Recommendation recommendation;
    recommendation.id = "recommendation/signal/relation/causes";
    recommendation.priority = "high";
    recommendation.action = "review repeated relation cluster";
    recommendation.reason = signal.explanation;
    recommendation.signals = {signal};

    DecisionReport report;
    report.branch = "main";
    report.signals = {signal};
    report.recommendations = {recommendation};

    const auto text = render_decision_report_text(report);
    REQUIRE(text.find("High priority:") != std::string::npos);
    REQUIRE(text.find("causes repeats 4 times") != std::string::npos);
    REQUIRE(text.find("- review repeated relation cluster") != std::string::npos);
}
