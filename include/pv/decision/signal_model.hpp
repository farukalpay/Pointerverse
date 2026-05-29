// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "pv/hash/canonical.hpp"
#include "pv/decision/decision_report.hpp"
#include "pv/projection/entity_projection.hpp"
#include "pv/projection/relation_projection.hpp"

namespace pv {

struct SignalThresholdPolicy {
    Hash256 baseline_hash;
    double medium_quantile{0.80};
    double high_quantile{0.95};
    double critical_quantile{0.99};
};

struct SignalModelOptions {
    SignalThresholdPolicy thresholds;
};

class SignalModel {
public:
    explicit SignalModel(SignalModelOptions options = {});

    [[nodiscard]] std::vector<Signal> signals(
        const std::vector<EntityProjectionEntry>& entities,
        const std::vector<RelationProjectionEntry>& relations) const;
    [[nodiscard]] std::vector<Recommendation> recommendations(
        const std::vector<Signal>& signals) const;
    [[nodiscard]] DecisionReport report(
        std::string_view branch,
        const std::vector<EntityProjectionEntry>& entities,
        const std::vector<RelationProjectionEntry>& relations) const;

private:
    SignalModelOptions options_;
};

}  // namespace pv
