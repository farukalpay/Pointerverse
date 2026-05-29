// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pv/core/id.hpp"
#include "pv/core/pointer.hpp"
#include "pv/hash/canonical.hpp"
#include "pv/runtime/ids.hpp"

namespace pv {

class Repository;
struct WorldGraphIndexEntry;
struct WorldSnapshot;

struct WeightedArc {
    ObjectId from;
    ObjectId to;
    PointerId pointer;
    std::uint64_t weight{0};
    std::string relation;

    friend bool operator==(const WeightedArc&, const WeightedArc&) = default;
};

struct WeightedGraphView {
    CommitId commit;
    Hash256 world_root;
    std::vector<ObjectId> objects;
    std::vector<WeightedArc> arcs;

    friend bool operator==(const WeightedGraphView&, const WeightedGraphView&) = default;
};

[[nodiscard]] std::uint64_t canonical_weight(Weight weight) noexcept;
void canonicalize(WeightedGraphView& graph);
[[nodiscard]] WeightedGraphView canonical_weighted_graph_view(WeightedGraphView graph);
[[nodiscard]] WeightedGraphView weighted_graph_view_from_snapshot(
    CommitId commit,
    Hash256 world_root,
    const WorldSnapshot& snapshot);
[[nodiscard]] WeightedGraphView weighted_graph_view_from_index(const WorldGraphIndexEntry& entry);
[[nodiscard]] WeightedGraphView weighted_graph_view_for_commit(const Repository& repository, CommitId commit);
[[nodiscard]] Hash256 weighted_graph_view_hash(WeightedGraphView graph);

}  // namespace pv
