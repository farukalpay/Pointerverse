// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string_view>

#include "pv/law/verifier.hpp"
#include "pv/measure/risk_evidence.hpp"
#include "pv/runtime/ids.hpp"

namespace pv {

class Repository;

struct RepairSearchOptions {
    std::uint32_t max_depth{3};
    std::uint32_t max_candidates{256};
};

class RepairDistanceMeasure {
public:
    [[nodiscard]] MeasuredComponent measure(
        const Repository& repository,
        std::string_view branch,
        CommitId commit,
        const Verifier* verifier,
        RepairSearchOptions options = {}) const;

    [[nodiscard]] MeasuredComponent measure(
        const Repository& repository,
        std::string_view branch,
        CommitId commit,
        const Verifier& verifier,
        RepairSearchOptions options = {}) const;
};

}  // namespace pv

