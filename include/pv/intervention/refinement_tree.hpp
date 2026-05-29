// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <vector>

#include "pv/intervention/operator_family.hpp"

namespace pv {

struct RefinementNode {
    ScaleValue scale{ScaleValue::zero()};
    std::uint8_t depth{0};
};

struct RefinementTree {
    OperatorFamily family;
    std::uint8_t max_depth{0};
    std::vector<RefinementNode> nodes;
};

[[nodiscard]] RefinementTree build_refinement_tree(const OperatorFamily& family, std::uint8_t max_depth);
[[nodiscard]] std::vector<InterventionOperator> refine_family(const OperatorFamily& family, std::uint8_t max_depth);

}  // namespace pv
