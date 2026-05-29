// SPDX-License-Identifier: Apache-2.0
#include "pv/measure/graph_functional.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <vector>

#include "pv/hash/hasher.hpp"
#include "pv/kernel/canonical_codec.hpp"
#include "pv/measure/cut_analysis.hpp"

namespace pv {
namespace {

constexpr std::uint64_t weight_scale = 1000000ULL;

struct IndexedGraph {
    WeightedGraphView graph;
    std::map<ObjectId, std::size_t> object_index;
    std::vector<std::vector<std::size_t>> outgoing;
    std::vector<std::vector<std::size_t>> incoming;
};

bool pointer_less(PointerId left, PointerId right) noexcept {
    return left.value < right.value;
}

void sort_objects(std::vector<ObjectId>& objects) {
    std::ranges::sort(objects, [](ObjectId left, ObjectId right) {
        return left < right;
    });
    objects.erase(std::ranges::unique(objects).begin(), objects.end());
}

void sort_pointers(std::vector<PointerId>& pointers) {
    std::ranges::sort(pointers, pointer_less);
    pointers.erase(std::ranges::unique(pointers).begin(), pointers.end());
}

std::uint64_t saturating_add(std::uint64_t left, std::uint64_t right) noexcept {
    constexpr auto max = std::numeric_limits<std::uint64_t>::max();
    if (max - left < right) {
        return max;
    }
    return left + right;
}

std::uint64_t saturating_mul_div(std::uint64_t left, std::uint64_t right, std::uint64_t denominator) noexcept {
    if (denominator == 0) {
        return std::numeric_limits<std::uint64_t>::max();
    }
#if defined(__SIZEOF_INT128__)
    const auto product = static_cast<unsigned __int128>(left) * static_cast<unsigned __int128>(right);
    const auto divided = product / denominator;
    if (divided > std::numeric_limits<std::uint64_t>::max()) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return static_cast<std::uint64_t>(divided);
#else
    return saturating_mul(left / denominator, right);
#endif
}

std::uint64_t attenuate(std::uint64_t mass, std::uint64_t weight, std::uint64_t numerator, std::uint64_t denominator) noexcept {
    const auto weighted = saturating_mul_div(mass, weight, weight_scale);
    return saturating_mul_div(weighted, numerator, denominator);
}

IndexedGraph index_graph(const WeightedGraphView& input) {
    IndexedGraph indexed;
    indexed.graph = canonical_weighted_graph_view(input);
    indexed.outgoing.resize(indexed.graph.objects.size());
    indexed.incoming.resize(indexed.graph.objects.size());
    for (std::size_t index = 0; index < indexed.graph.objects.size(); ++index) {
        indexed.object_index.emplace(indexed.graph.objects[index], index);
    }
    for (std::size_t arc_index = 0; arc_index < indexed.graph.arcs.size(); ++arc_index) {
        const auto& arc = indexed.graph.arcs[arc_index];
        const auto from = indexed.object_index.find(arc.from);
        const auto to = indexed.object_index.find(arc.to);
        if (from == indexed.object_index.end() || to == indexed.object_index.end()) {
            continue;
        }
        indexed.outgoing[from->second].push_back(arc_index);
        indexed.incoming[to->second].push_back(arc_index);
    }
    return indexed;
}

std::vector<std::size_t> seed_indices(const IndexedGraph& indexed, std::span<const ObjectId> seeds) {
    std::vector<ObjectId> sorted{seeds.begin(), seeds.end()};
    sort_objects(sorted);
    std::vector<std::size_t> out;
    out.reserve(sorted.size());
    for (const auto seed : sorted) {
        const auto iter = indexed.object_index.find(seed);
        if (iter != indexed.object_index.end()) {
            out.push_back(iter->second);
        }
    }
    return out;
}

void write_object(CanonicalWriter& writer, ObjectId id) {
    writer.u32(id.index);
    writer.u32(id.generation);
}

void write_pointer(CanonicalWriter& writer, PointerId id) {
    writer.u64(id.value);
}

FunctionalResult directed_cone(
    const WeightedGraphView& graph,
    std::span<const ObjectId> seeds,
    bool reverse) {
    const auto indexed = index_graph(graph);
    const auto roots = seed_indices(indexed, seeds);
    std::vector<bool> visited_objects(indexed.graph.objects.size(), false);
    std::set<std::uint64_t> visited_pointers;
    std::vector<std::size_t> frontier;
    frontier.reserve(roots.size());
    for (const auto root : roots) {
        if (!visited_objects[root]) {
            visited_objects[root] = true;
            frontier.push_back(root);
        }
    }

    std::uint64_t weighted_mass = 0;
    while (!frontier.empty()) {
        std::vector<std::size_t> next;
        for (const auto node : frontier) {
            const auto& arcs = reverse ? indexed.incoming[node] : indexed.outgoing[node];
            for (const auto arc_index : arcs) {
                const auto& arc = indexed.graph.arcs[arc_index];
                if (visited_pointers.insert(arc.pointer.value).second) {
                    weighted_mass = saturating_add(weighted_mass, arc.weight);
                }
                const auto target = indexed.object_index.at(reverse ? arc.from : arc.to);
                if (!visited_objects[target]) {
                    visited_objects[target] = true;
                    next.push_back(target);
                }
            }
        }
        std::ranges::sort(next);
        next.erase(std::ranges::unique(next).begin(), next.end());
        frontier = std::move(next);
    }

    FunctionalResult result;
    std::uint64_t object_count = 0;
    for (std::size_t index = 0; index < visited_objects.size(); ++index) {
        if (visited_objects[index]) {
            object_count += 1;
            result.witness_objects.push_back(indexed.graph.objects[index]);
        }
    }
    for (const auto pointer : visited_pointers) {
        result.witness_pointers.push_back(PointerId{pointer});
    }
    result.value = object_count;
    result.value = saturating_add(result.value, visited_pointers.size());
    result.value = saturating_add(result.value, weighted_mass / weight_scale);
    std::ostringstream explanation;
    explanation << (reverse ? "reverse dependency" : "forward cone")
                << " objects: " << object_count
                << "; pointers: " << visited_pointers.size()
                << "; weighted mass: " << (weighted_mass / weight_scale);
    result.explanation = explanation.str();
    return result;
}

}  // namespace

std::string_view ForwardConeMass::name() const {
    return "forward_cone_mass";
}

FunctionalResult ForwardConeMass::evaluate(
    const WeightedGraphView& graph,
    std::span<const ObjectId> seeds) const {
    return directed_cone(graph, seeds, false);
}

std::string_view ReverseDependencyMass::name() const {
    return "reverse_dependency_mass";
}

FunctionalResult ReverseDependencyMass::evaluate(
    const WeightedGraphView& graph,
    std::span<const ObjectId> seeds) const {
    return directed_cone(graph, seeds, true);
}

std::string_view CutVertexImpact::name() const {
    return "cut_vertex_impact";
}

FunctionalResult CutVertexImpact::evaluate(
    const WeightedGraphView& graph,
    std::span<const ObjectId> seeds) const {
    auto canonical = canonical_weighted_graph_view(graph);
    const auto cuts = analyze_cuts(canonical);
    std::set<ObjectId> seed_set;
    for (const auto seed : seeds) {
        seed_set.insert(seed);
    }

    FunctionalResult result;
    for (const auto object : cuts.articulation_points) {
        if (!seed_set.contains(object)) {
            continue;
        }
        result.witness_objects.push_back(object);
        const auto loss = cuts.component_loss.find(object);
        if (loss != cuts.component_loss.end()) {
            result.value = saturating_add(result.value, loss->second);
        }
    }

    for (const auto pointer : cuts.bridges) {
        const auto arc = std::ranges::find(canonical.arcs, pointer, &WeightedArc::pointer);
        if (arc == canonical.arcs.end()) {
            continue;
        }
        if (seed_set.contains(arc->from) || seed_set.contains(arc->to)) {
            result.witness_pointers.push_back(pointer);
            result.value = saturating_add(result.value, 1);
        }
    }

    sort_objects(result.witness_objects);
    sort_pointers(result.witness_pointers);
    std::ostringstream explanation;
    explanation << "articulation witnesses: " << result.witness_objects.size()
                << "; bridge witnesses: " << result.witness_pointers.size()
                << "; component loss: " << result.value;
    result.explanation = explanation.str();
    return result;
}

PathMultiplicity::PathMultiplicity(PathMultiplicityOptions options) : options_(options) {}

std::string_view PathMultiplicity::name() const {
    return "path_multiplicity";
}

FunctionalResult PathMultiplicity::evaluate(
    const WeightedGraphView& graph,
    std::span<const ObjectId> seeds) const {
    const auto indexed = index_graph(graph);
    const auto roots = seed_indices(indexed, seeds);

    FunctionalResult result;
    std::uint64_t path_count = 0;
    std::uint64_t weighted_path_mass = 0;
    std::uint32_t max_depth_reached = 0;
    bool truncated = false;
    std::set<ObjectId> witness_objects;
    std::set<std::uint64_t> witness_pointers;

    auto count_path = [&](std::uint32_t depth, std::uint64_t mass) {
        path_count = saturating_add(path_count, 1);
        weighted_path_mass = saturating_add(weighted_path_mass, mass);
        max_depth_reached = std::max(max_depth_reached, depth);
        if (path_count >= options_.max_paths) {
            truncated = true;
        }
    };

    auto dfs = [&](auto&& self, std::size_t node, std::uint32_t depth, std::uint64_t mass, std::vector<bool>& in_path) -> void {
        if (truncated || depth >= options_.max_depth) {
            return;
        }
        for (const auto arc_index : indexed.outgoing[node]) {
            if (truncated) {
                return;
            }
            const auto& arc = indexed.graph.arcs[arc_index];
            const auto next = indexed.object_index.at(arc.to);
            if (in_path[next]) {
                continue;
            }
            const auto next_mass = attenuate(mass, arc.weight, options_.attenuation_num, options_.attenuation_den);
            witness_objects.insert(arc.from);
            witness_objects.insert(arc.to);
            witness_pointers.insert(arc.pointer.value);
            count_path(depth + 1U, next_mass);
            in_path[next] = true;
            self(self, next, depth + 1U, next_mass, in_path);
            in_path[next] = false;
        }
    };

    for (const auto root : roots) {
        if (truncated) {
            break;
        }
        std::vector<bool> in_path(indexed.graph.objects.size(), false);
        in_path[root] = true;
        witness_objects.insert(indexed.graph.objects[root]);
        if (options_.include_length_zero) {
            count_path(0, weight_scale);
        }
        dfs(dfs, root, 0, weight_scale, in_path);
    }

    result.value = path_count;
    result.value = saturating_add(result.value, weighted_path_mass / weight_scale);
    result.witness_objects.assign(witness_objects.begin(), witness_objects.end());
    for (const auto pointer : witness_pointers) {
        result.witness_pointers.push_back(PointerId{pointer});
    }
    std::ostringstream explanation;
    explanation << "simple paths: " << path_count
                << "; weighted path mass: " << (weighted_path_mass / weight_scale)
                << "; max depth reached: " << max_depth_reached
                << "; truncated: " << (truncated ? "true" : "false");
    result.explanation = explanation.str();
    return result;
}

std::string_view BoundaryExpansion::name() const {
    return "boundary_expansion";
}

FunctionalResult BoundaryExpansion::evaluate(
    const WeightedGraphView& graph,
    std::span<const ObjectId> seeds) const {
    const auto indexed = index_graph(graph);
    std::set<ObjectId> seed_set;
    for (const auto seed : seeds) {
        if (indexed.object_index.contains(seed)) {
            seed_set.insert(seed);
        }
    }

    FunctionalResult result;
    std::uint64_t weighted_mass = 0;
    for (const auto& arc : indexed.graph.arcs) {
        const auto from_seed = seed_set.contains(arc.from);
        const auto to_seed = seed_set.contains(arc.to);
        if (from_seed == to_seed) {
            continue;
        }
        result.witness_objects.push_back(arc.from);
        result.witness_objects.push_back(arc.to);
        result.witness_pointers.push_back(arc.pointer);
        result.value = saturating_add(result.value, 1);
        weighted_mass = saturating_add(weighted_mass, arc.weight);
    }
    result.value = saturating_add(result.value, weighted_mass / weight_scale);
    sort_objects(result.witness_objects);
    sort_pointers(result.witness_pointers);
    std::ostringstream explanation;
    explanation << "boundary arcs: " << result.witness_pointers.size()
                << "; weighted boundary mass: " << (weighted_mass / weight_scale);
    result.explanation = explanation.str();
    return result;
}

PropagatedMass::PropagatedMass(PropagationOptions options) : options_(options) {}

std::string_view PropagatedMass::name() const {
    return "propagated_mass";
}

FunctionalResult PropagatedMass::evaluate(
    const WeightedGraphView& graph,
    std::span<const ObjectId> seeds) const {
    const auto indexed = index_graph(graph);
    const auto roots = seed_indices(indexed, seeds);
    std::vector<std::uint64_t> current(indexed.graph.objects.size(), 0);
    std::vector<std::uint64_t> total(indexed.graph.objects.size(), 0);
    FunctionalResult result;
    std::set<std::uint64_t> witness_pointers;

    for (const auto root : roots) {
        current[root] = saturating_add(current[root], weight_scale);
    }

    std::uint32_t steps = 0;
    for (; steps <= options_.max_steps; ++steps) {
        bool any = false;
        for (std::size_t index = 0; index < current.size(); ++index) {
            if (current[index] == 0) {
                continue;
            }
            any = true;
            total[index] = saturating_add(total[index], current[index]);
        }
        if (!any || steps == options_.max_steps) {
            break;
        }

        std::vector<std::uint64_t> next(indexed.graph.objects.size(), 0);
        for (std::size_t index = 0; index < current.size(); ++index) {
            if (current[index] == 0) {
                continue;
            }
            for (const auto arc_index : indexed.outgoing[index]) {
                const auto& arc = indexed.graph.arcs[arc_index];
                const auto to = indexed.object_index.at(arc.to);
                const auto mass = attenuate(current[index], arc.weight, options_.attenuation_num, options_.attenuation_den);
                next[to] = saturating_add(next[to], mass);
                witness_pointers.insert(arc.pointer.value);
            }
        }
        current = std::move(next);
    }

    std::uint64_t total_mass = 0;
    for (std::size_t index = 0; index < total.size(); ++index) {
        if (total[index] == 0) {
            continue;
        }
        total_mass = saturating_add(total_mass, total[index]);
        result.witness_objects.push_back(indexed.graph.objects[index]);
    }
    for (const auto pointer : witness_pointers) {
        result.witness_pointers.push_back(PointerId{pointer});
    }
    result.value = total_mass / weight_scale;
    std::ostringstream explanation;
    explanation << "integer propagated mass: " << result.value
                << "; steps evaluated: " << steps
                << "; attenuation: " << options_.attenuation_num << "/" << options_.attenuation_den;
    result.explanation = explanation.str();
    return result;
}

Hash256 functional_result_hash(std::string_view name, FunctionalResult result) {
    sort_objects(result.witness_objects);
    sort_pointers(result.witness_pointers);
    CanonicalWriter writer;
    writer.string("FunctionalResult:v1");
    writer.string(name);
    writer.u64(result.value);
    writer.u64(result.witness_objects.size());
    for (const auto object : result.witness_objects) {
        write_object(writer, object);
    }
    writer.u64(result.witness_pointers.size());
    for (const auto pointer : result.witness_pointers) {
        write_pointer(writer, pointer);
    }
    writer.string(result.explanation);
    return sha256(writer.bytes());
}

}  // namespace pv
