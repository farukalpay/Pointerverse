// SPDX-License-Identifier: Apache-2.0
#include "pv/measure/cut_analysis.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <vector>

namespace pv {
namespace {

struct Edge {
    std::size_t to{0};
    PointerId pointer;
};

bool object_less(ObjectId left, ObjectId right) noexcept {
    return left < right;
}

bool pointer_less(PointerId left, PointerId right) noexcept {
    return left.value < right.value;
}

std::size_t index_of(const std::vector<ObjectId>& objects, ObjectId object) {
    const auto iter = std::ranges::lower_bound(objects, object, object_less);
    if (iter == objects.end() || *iter != object) {
        return objects.size();
    }
    return static_cast<std::size_t>(iter - objects.begin());
}

std::uint64_t loss_without(
    const std::vector<std::vector<Edge>>& adjacency,
    std::size_t removed) {
    const auto total = adjacency.size();
    if (total <= 1) {
        return 0;
    }

    std::vector<bool> seen(total, false);
    seen[removed] = true;
    std::size_t largest = 0;
    for (std::size_t start = 0; start < total; ++start) {
        if (seen[start]) {
            continue;
        }
        std::vector<std::size_t> stack{start};
        seen[start] = true;
        std::size_t size = 0;
        while (!stack.empty()) {
            const auto node = stack.back();
            stack.pop_back();
            size += 1;
            for (const auto& edge : adjacency[node]) {
                if (!seen[edge.to]) {
                    seen[edge.to] = true;
                    stack.push_back(edge.to);
                }
            }
        }
        largest = std::max(largest, size);
    }
    return static_cast<std::uint64_t>((total - 1U) - largest);
}

}  // namespace

CutAnalysis analyze_cuts(const WeightedGraphView& input) {
    auto graph = canonical_weighted_graph_view(input);
    CutAnalysis analysis;
    if (graph.objects.empty()) {
        return analysis;
    }

    std::vector<std::vector<Edge>> adjacency(graph.objects.size());
    for (const auto& arc : graph.arcs) {
        const auto from = index_of(graph.objects, arc.from);
        const auto to = index_of(graph.objects, arc.to);
        if (from >= graph.objects.size() || to >= graph.objects.size()) {
            continue;
        }
        adjacency[from].push_back(Edge{to, arc.pointer});
        adjacency[to].push_back(Edge{from, arc.pointer});
    }
    for (auto& edges : adjacency) {
        std::ranges::sort(edges, [](const Edge& left, const Edge& right) {
            if (left.to != right.to) {
                return left.to < right.to;
            }
            return left.pointer.value < right.pointer.value;
        });
    }

    std::vector<int> discovered(graph.objects.size(), -1);
    std::vector<int> low(graph.objects.size(), -1);
    std::vector<std::size_t> parent(graph.objects.size(), graph.objects.size());
    std::set<std::size_t> articulation;
    std::set<std::uint64_t> bridges;
    int time = 0;

    std::function<void(std::size_t, PointerId)> dfs = [&](std::size_t node, PointerId parent_edge) {
        discovered[node] = low[node] = time++;
        std::size_t children = 0;
        for (const auto& edge : adjacency[node]) {
            if (edge.pointer == parent_edge) {
                continue;
            }
            if (discovered[edge.to] < 0) {
                parent[edge.to] = node;
                children += 1;
                dfs(edge.to, edge.pointer);
                low[node] = std::min(low[node], low[edge.to]);
                if (parent[node] == graph.objects.size()) {
                    if (children > 1) {
                        articulation.insert(node);
                    }
                } else if (low[edge.to] >= discovered[node]) {
                    articulation.insert(node);
                }
                if (low[edge.to] > discovered[node]) {
                    bridges.insert(edge.pointer.value);
                }
            } else {
                low[node] = std::min(low[node], discovered[edge.to]);
            }
        }
    };

    for (std::size_t index = 0; index < graph.objects.size(); ++index) {
        if (discovered[index] < 0) {
            dfs(index, PointerId{0});
        }
    }

    analysis.articulation_points.reserve(articulation.size());
    for (const auto index : articulation) {
        analysis.articulation_points.push_back(graph.objects[index]);
        analysis.component_loss.emplace(graph.objects[index], loss_without(adjacency, index));
    }
    std::ranges::sort(analysis.articulation_points, object_less);

    analysis.bridges.reserve(bridges.size());
    for (const auto pointer : bridges) {
        analysis.bridges.push_back(PointerId{pointer});
    }
    std::ranges::sort(analysis.bridges, pointer_less);
    return analysis;
}

}  // namespace pv
