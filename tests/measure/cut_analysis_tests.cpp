// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "pv/measure/cut_analysis.hpp"

using namespace pv;

namespace {

ObjectId object(std::uint32_t index) {
    return ObjectId{index, 0};
}

PointerId pointer(std::uint64_t value) {
    return PointerId{value};
}

WeightedArc arc(std::uint32_t from, std::uint32_t to, std::uint64_t id) {
    return WeightedArc{object(from), object(to), pointer(id), 1000000, "causes"};
}

bool contains_object(const std::vector<ObjectId>& objects, ObjectId object) {
    return std::ranges::find(objects, object) != objects.end();
}

bool contains_pointer(const std::vector<PointerId>& pointers, PointerId pointer) {
    return std::ranges::find(pointers, pointer) != pointers.end();
}

}  // namespace

TEST_CASE("cut analysis detects true articulation point and bridges") {
    WeightedGraphView graph;
    graph.objects = {object(1), object(2), object(3)};
    graph.arcs = {arc(1, 2, 1), arc(2, 3, 2)};

    const auto cuts = analyze_cuts(graph);

    REQUIRE(contains_object(cuts.articulation_points, object(2)));
    REQUIRE(contains_pointer(cuts.bridges, pointer(1)));
    REQUIRE(contains_pointer(cuts.bridges, pointer(2)));
    REQUIRE(cuts.component_loss.at(object(2)) == 1);
}

TEST_CASE("cut analysis does not mark mere in and out degree as articulation") {
    WeightedGraphView graph;
    graph.objects = {object(1), object(2), object(3)};
    graph.arcs = {
        arc(1, 2, 1),
        arc(2, 3, 2),
        arc(1, 3, 3)
    };

    const auto cuts = analyze_cuts(graph);

    REQUIRE_FALSE(contains_object(cuts.articulation_points, object(2)));
}
