// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pv/measure/risk_evidence.hpp"
#include "pv/runtime/commit_record.hpp"

namespace pv {

class LawRiskMeasure {
public:
    [[nodiscard]] MeasuredComponent measure(const CommitRecord& record) const;
};

[[nodiscard]] std::uint64_t canonical_fixed_magnitude(double magnitude) noexcept;

}  // namespace pv

