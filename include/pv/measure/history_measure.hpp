// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

#include "pv/measure/risk_evidence.hpp"
#include "pv/runtime/ids.hpp"

namespace pv {

class Repository;

struct HistoryFrequency {
    std::uint64_t total_commits{0};
    std::map<std::string, std::uint64_t> operation_counts;
    std::map<std::string, std::uint64_t> relation_counts;
    std::map<std::string, std::uint64_t> type_pair_counts;
    std::map<std::string, std::uint64_t> touched_object_counts;
};

[[nodiscard]] std::uint64_t neg_log2_scaled(std::uint64_t numerator, std::uint64_t denominator) noexcept;

class HistorySurpriseMeasure {
public:
    [[nodiscard]] MeasuredComponent measure(
        const Repository& repository,
        std::string_view branch,
        CommitId commit) const;
};

}  // namespace pv

