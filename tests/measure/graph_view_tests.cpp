// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "pv/measure/graph_view.hpp"

using namespace pv;

namespace {

ObjectId object(std::uint32_t index) {
    return ObjectId{index, 0};
}

PointerId pointer(std::uint64_t value) {
    return PointerId{value};
}

}  // namespace

TEST_CASE("weighted graph view canonicalizes objects and arcs") {
    WeightedGraphView graph;
    graph.objects = {object(3), object(1), object(2), object(1)};
    graph.arcs = {
        WeightedArc{object(2), object(3), pointer(2), 1000000, "causes"},
        WeightedArc{object(1), object(2), pointer(1), 1000000, "causes"},
        WeightedArc{object(1), object(3), pointer(3), 1000000, "causes"}
    };

    canonicalize(graph);

    REQUIRE(graph.objects == std::vector<ObjectId>{object(1), object(2), object(3)});
    REQUIRE(graph.arcs[0].pointer == pointer(1));
    REQUIRE(graph.arcs[1].pointer == pointer(3));
    REQUIRE(graph.arcs[2].pointer == pointer(2));
}

TEST_CASE("same weighted graph has same hash under insertion order changes") {
    WeightedGraphView left;
    left.objects = {object(1), object(2), object(3)};
    left.arcs = {
        WeightedArc{object(1), object(2), pointer(1), 1000000, "causes"},
        WeightedArc{object(2), object(3), pointer(2), 500000, "causes"}
    };

    WeightedGraphView right;
    right.objects = {object(3), object(1), object(2)};
    right.arcs = {
        WeightedArc{object(2), object(3), pointer(2), 500000, "causes"},
        WeightedArc{object(1), object(2), pointer(1), 1000000, "causes"}
    };

    REQUIRE(weighted_graph_view_hash(left) == weighted_graph_view_hash(right));
}
