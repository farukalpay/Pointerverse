// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string_view>
#include <vector>

#include "pv/measure/risk_evidence.hpp"
#include "pv/runtime/ids.hpp"

namespace pv {

class Repository;

class StructuralRiskMeasure {
public:
    [[nodiscard]] std::vector<MeasuredComponent> measure_components(
        const Repository& repository,
        std::string_view branch,
        CommitId commit) const;

    [[nodiscard]] MeasuredComponent measure(
        const Repository& repository,
        std::string_view branch,
        CommitId commit) const;
};

}  // namespace pv
