// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string_view>

#include "pv/measure/risk_evidence.hpp"
#include "pv/runtime/ids.hpp"

namespace pv {

class Repository;

class StructuralRiskMeasure {
public:
    [[nodiscard]] MeasuredComponent measure(
        const Repository& repository,
        std::string_view branch,
        CommitId commit) const;
};

}  // namespace pv

