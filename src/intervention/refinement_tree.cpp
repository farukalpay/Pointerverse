// SPDX-License-Identifier: Apache-2.0
#include "pv/intervention/refinement_tree.hpp"

#include <algorithm>

namespace pv {

RefinementTree build_refinement_tree(const OperatorFamily& family, std::uint8_t max_depth) {
    RefinementTree tree;
    tree.family = family;
    tree.max_depth = max_depth;
    const auto scales = dyadic_refinement_scales(max_depth);
    tree.nodes.reserve(scales.size());
    for (const auto scale : scales) {
        tree.nodes.push_back(RefinementNode{scale, max_depth});
    }
    return tree;
}

std::vector<InterventionOperator> refine_family(const OperatorFamily& family, std::uint8_t max_depth) {
    const auto tree = build_refinement_tree(family, max_depth);
    std::vector<InterventionOperator> operators;
    operators.reserve(tree.nodes.size());
    for (const auto& node : tree.nodes) {
        if (node.scale == ScaleValue::zero()) {
            continue;
        }
        operators.push_back(make_operator(family, node.scale));
    }
    std::ranges::sort(operators, [](const auto& left, const auto& right) {
        if (left.scale != right.scale) {
            return left.scale < right.scale;
        }
        return to_hex(left.canonical_hash) < to_hex(right.canonical_hash);
    });
    return operators;
}

}  // namespace pv
