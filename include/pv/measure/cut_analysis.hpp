// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <map>
#include <vector>

#include "pv/core/id.hpp"
#include "pv/core/pointer.hpp"
#include "pv/measure/graph_view.hpp"

namespace pv {

struct CutAnalysis {
    std::vector<ObjectId> articulation_points;
    std::vector<PointerId> bridges;
    std::map<ObjectId, std::uint64_t> component_loss;
};

[[nodiscard]] CutAnalysis analyze_cuts(const WeightedGraphView& graph);

}  // namespace pv
