// SPDX-License-Identifier: Apache-2.0
#include "pv/measure/risk_value.hpp"

#include <algorithm>
#include <limits>

#include "pv/hash/hasher.hpp"
#include "pv/kernel/canonical_codec.hpp"

namespace pv {
namespace {

std::uint64_t saturating_add(std::uint64_t left, std::uint64_t right) noexcept {
    constexpr auto max = std::numeric_limits<std::uint64_t>::max();
    if (max - left < right) {
        return max;
    }
    return left + right;
}

std::uint64_t saturating_mul(std::uint64_t left, std::uint64_t right) noexcept {
    if (left == 0 || right == 0) {
        return 0;
    }
    constexpr auto max = std::numeric_limits<std::uint64_t>::max();
    if (left > max / right) {
        return max;
    }
    return left * right;
}

bool coordinate_less(const RiskCoordinate& left, const RiskCoordinate& right) {
    if (left.namespace_id != right.namespace_id) {
        return left.namespace_id < right.namespace_id;
    }
    return left.component_id < right.component_id;
}

bool same_coordinate(const RiskCoordinate& left, const RiskCoordinate& right) {
    return left.namespace_id == right.namespace_id && left.component_id == right.component_id;
}

}  // namespace

bool empty(RiskVector value) noexcept {
    return value.structural == 0
        && value.law_distance == 0
        && value.repair_distance == 0
        && value.surprise == 0;
}

bool empty(const RiskLatticeElement& value) noexcept {
    return std::ranges::all_of(value.coordinates, [](const auto& coordinate) {
        return coordinate.value == 0;
    });
}

RiskLatticeElement canonical_risk_lattice(RiskLatticeElement value) {
    std::ranges::sort(value.coordinates, coordinate_less);
    std::vector<RiskCoordinate> out;
    out.reserve(value.coordinates.size());
    for (const auto& coordinate : value.coordinates) {
        if (coordinate.namespace_id.empty() || coordinate.component_id.empty()) {
            continue;
        }
        if (!out.empty() && same_coordinate(out.back(), coordinate)) {
            out.back().value = std::max(out.back().value, coordinate.value);
        } else {
            out.push_back(coordinate);
        }
    }
    value.coordinates = std::move(out);
    return value;
}

std::uint64_t coordinate_value(
    const RiskLatticeElement& value,
    std::string_view namespace_id,
    std::string_view component_id) noexcept {
    for (const auto& coordinate : value.coordinates) {
        if (coordinate.namespace_id == namespace_id && coordinate.component_id == component_id) {
            return coordinate.value;
        }
    }
    return 0;
}

RiskVector risk_vector_from_lattice(const RiskLatticeElement& value) noexcept {
    RiskVector out;
    out.structural = saturating_add(out.structural, coordinate_value(value, "structural", "forward_cone_mass"));
    out.structural = saturating_add(out.structural, coordinate_value(value, "structural", "reverse_dependency_mass") / 2U);
    out.structural = saturating_add(out.structural, saturating_mul(coordinate_value(value, "structural", "cut_vertex_impact"), 2U));
    out.structural = saturating_add(out.structural, coordinate_value(value, "structural", "boundary_expansion"));
    out.structural = saturating_add(out.structural, coordinate_value(value, "structural", "path_multiplicity") / 4U);
    out.structural = saturating_add(out.structural, coordinate_value(value, "structural", "propagated_mass"));
    out.law_distance = coordinate_value(value, "law", "total_magnitude");
    out.repair_distance = coordinate_value(value, "repair", "distance");
    out.surprise = coordinate_value(value, "surprise", "history_distance");
    return out;
}

RiskLatticeElement risk_lattice_from_vector(RiskVector value) {
    RiskLatticeElement out;
    if (value.structural != 0) {
        out.coordinates.push_back(RiskCoordinate{"structural", "compat_projection", value.structural});
    }
    if (value.law_distance != 0) {
        out.coordinates.push_back(RiskCoordinate{"law", "total_magnitude", value.law_distance});
    }
    if (value.repair_distance != 0) {
        out.coordinates.push_back(RiskCoordinate{"repair", "distance", value.repair_distance});
    }
    if (value.surprise != 0) {
        out.coordinates.push_back(RiskCoordinate{"surprise", "history_distance", value.surprise});
    }
    return canonical_risk_lattice(std::move(out));
}

Hash256 risk_lattice_hash(RiskLatticeElement value) {
    value = canonical_risk_lattice(std::move(value));
    CanonicalWriter writer;
    writer.string("RiskLatticeElement:v1");
    writer.u64(value.coordinates.size());
    for (const auto& coordinate : value.coordinates) {
        writer.string(coordinate.namespace_id);
        writer.string(coordinate.component_id);
        writer.u64(coordinate.value);
    }
    return sha256(writer.bytes());
}

}  // namespace pv
