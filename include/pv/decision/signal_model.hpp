// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "pv/decision/decision_report.hpp"
#include "pv/projection/entity_projection.hpp"
#include "pv/projection/relation_projection.hpp"

namespace pv {

struct SignalModelOptions {
    std::size_t high_activity_entity_threshold{3};
    std::size_t repeated_relation_threshold{2};
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
