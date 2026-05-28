// SPDX-License-Identifier: Apache-2.0
#include "pv/runtime/commit_graph.hpp"

#include <algorithm>
#include <set>

#include "pv/hash/canonical.hpp"

namespace pv {

void CommitGraph::add_node(CommitNode node) {
    if (contains(node.id)) {
        return;
    }

    for (const auto& parent : node.parents) {
        if (auto* parent_node = const_cast<CommitNode*>(this->node(parent))) {
            parent_node->children.push_back(node.id);
        }
    }

    nodes_.push_back(std::move(node));
}

bool CommitGraph::contains(CommitId id) const noexcept {
    return node(id) != nullptr;
}

const CommitNode* CommitGraph::node(CommitId id) const noexcept {
    for (const auto& candidate : nodes_) {
        if (candidate.id == id) {
            return &candidate;
        }
    }
    return nullptr;
}

std::vector<CommitId> CommitGraph::path_to_root(CommitId id) const {
    std::vector<CommitId> out;
    auto current = id;
    while (auto* current_node = node(current)) {
        out.push_back(current);
        if (current_node->parents.empty()) {
            break;
        }
        current = current_node->parents.front();
    }
    return out;
}

std::optional<CommitId> CommitGraph::common_ancestor(CommitId left, CommitId right) const {
    const auto left_path = path_to_root(left);
    std::set<std::string> left_ancestors;
    for (const auto& id : left_path) {
        left_ancestors.insert(to_hex(id.value));
    }

    for (const auto& id : path_to_root(right)) {
        if (left_ancestors.contains(to_hex(id.value))) {
            return id;
        }
    }
    return std::nullopt;
}

}  // namespace pv
