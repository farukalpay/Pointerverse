// SPDX-License-Identifier: Apache-2.0
#include "pv/measure/graph_view.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "pv/hash/hasher.hpp"
#include "pv/kernel/canonical_codec.hpp"
#include "pv/storage/repository.hpp"
#include "pv/storage/world_index_store.hpp"

namespace pv {
namespace {

bool active_at(const PointerSnapshot& pointer, Epoch epoch) noexcept {
    return pointer.born_at <= epoch && (!pointer.expires_at.has_value() || epoch < *pointer.expires_at);
}

bool object_less(ObjectId left, ObjectId right) noexcept {
    return left < right;
}

bool arc_less(const WeightedArc& left, const WeightedArc& right) noexcept {
    if (left.from != right.from) {
        return object_less(left.from, right.from);
    }
    if (left.to != right.to) {
        return object_less(left.to, right.to);
    }
    return left.pointer.value < right.pointer.value;
}

void write_object(CanonicalWriter& writer, ObjectId id) {
    writer.u32(id.index);
    writer.u32(id.generation);
}

void write_pointer(CanonicalWriter& writer, PointerId id) {
    writer.u64(id.value);
}

}  // namespace

std::uint64_t canonical_weight(Weight weight) noexcept {
    if (!std::isfinite(weight.value) || weight.value <= 0.0) {
        return 0;
    }
    constexpr double scale = 1000000.0;
    if (weight.value >= static_cast<double>(std::numeric_limits<std::uint64_t>::max()) / scale) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return static_cast<std::uint64_t>(std::llround(weight.value * scale));
}

void canonicalize(WeightedGraphView& graph) {
    std::ranges::sort(graph.objects, object_less);
    graph.objects.erase(std::ranges::unique(graph.objects).begin(), graph.objects.end());

    std::ranges::sort(graph.arcs, arc_less);
    graph.arcs.erase(std::ranges::unique(graph.arcs, {}, &WeightedArc::pointer).begin(), graph.arcs.end());
}

WeightedGraphView canonical_weighted_graph_view(WeightedGraphView graph) {
    canonicalize(graph);
    return graph;
}

WeightedGraphView weighted_graph_view_from_snapshot(
    CommitId commit,
    Hash256 world_root,
    const WorldSnapshot& snapshot) {
    WeightedGraphView graph;
    graph.commit = commit;
    graph.world_root = world_root;
    graph.objects.reserve(snapshot.objects.size());
    graph.arcs.reserve(snapshot.pointers.size());

    for (const auto& object : snapshot.objects) {
        graph.objects.push_back(object.id);
    }
    for (const auto& pointer : snapshot.pointers) {
        if (!active_at(pointer, snapshot.epoch)) {
            continue;
        }
        graph.arcs.push_back(WeightedArc{
            pointer.from,
            pointer.to,
            pointer.id,
            canonical_weight(pointer.weight),
            snapshot.relation_name(pointer.relation)
        });
    }
    canonicalize(graph);
    return graph;
}

WeightedGraphView weighted_graph_view_from_index(const WorldGraphIndexEntry& entry) {
    WeightedGraphView graph;
    graph.commit = entry.commit;
    graph.world_root = entry.snapshot;
    graph.objects.reserve(entry.adjacency.size() + entry.names.size());
    graph.arcs.reserve(entry.pointers.size());

    for (const auto& adjacency : entry.adjacency) {
        graph.objects.push_back(adjacency.object);
    }
    for (const auto& name : entry.names) {
        graph.objects.push_back(name.object);
    }
    for (const auto& pointer : entry.pointers) {
        graph.arcs.push_back(WeightedArc{
            pointer.from,
            pointer.to,
            pointer.pointer,
            pointer.weight_scaled,
            pointer.relation
        });
    }
    canonicalize(graph);
    return graph;
}

WeightedGraphView weighted_graph_view_for_commit(const Repository& repository, CommitId commit) {
    const auto record = repository.backend().commit_record(commit);
    if (auto indexed = repository.backend().world_index().find_commit(commit);
        indexed.has_value() && indexed->snapshot == record.after_root) {
        return weighted_graph_view_from_index(*indexed);
    }
    const auto snapshot = repository.backend().snapshot(commit);
    return weighted_graph_view_from_snapshot(commit, record.after_root, snapshot);
}

Hash256 weighted_graph_view_hash(WeightedGraphView graph) {
    canonicalize(graph);
    CanonicalWriter writer;
    writer.string("WeightedGraphView:v1");
    writer.hash(graph.commit.value);
    writer.hash(graph.world_root);
    writer.u64(graph.objects.size());
    for (const auto object : graph.objects) {
        write_object(writer, object);
    }
    writer.u64(graph.arcs.size());
    for (const auto& arc : graph.arcs) {
        write_object(writer, arc.from);
        write_object(writer, arc.to);
        write_pointer(writer, arc.pointer);
        writer.u64(arc.weight);
        writer.string(arc.relation);
    }
    return sha256(writer.bytes());
}

}  // namespace pv
