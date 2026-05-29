// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "pv/measure/law_measure.hpp"

using namespace pv;

TEST_CASE("law risk equals violation magnitude not severity label") {
    CommitRecord record;
    record.violations.push_back(LawViolation{"low", Severity::Info, 2.0, "info magnitude"});
    record.violations.push_back(LawViolation{"fatal", Severity::Fatal, 1.0, "fatal magnitude"});

    const auto measured = LawRiskMeasure{}.measure(record);

    REQUIRE(measured.value == 3);
    REQUIRE(measured.evidence.laws.size() == 2);
}

