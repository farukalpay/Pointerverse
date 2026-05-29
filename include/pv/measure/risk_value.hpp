// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "pv/hash/canonical.hpp"

namespace pv {

struct RiskCoordinate {
    std::string namespace_id;
    std::string component_id;
    std::uint64_t value{0};

    friend bool operator==(const RiskCoordinate&, const RiskCoordinate&) = default;
};

struct RiskLatticeElement {
    std::vector<RiskCoordinate> coordinates;

    friend bool operator==(const RiskLatticeElement&, const RiskLatticeElement&) = default;
};

struct RiskVector {
    std::uint64_t structural{0};
    std::uint64_t law_distance{0};
    std::uint64_t repair_distance{0};
    std::uint64_t surprise{0};

    friend bool operator==(RiskVector, RiskVector) = default;
};

[[nodiscard]] bool empty(RiskVector value) noexcept;
[[nodiscard]] bool empty(const RiskLatticeElement& value) noexcept;
[[nodiscard]] RiskLatticeElement canonical_risk_lattice(RiskLatticeElement value);
[[nodiscard]] std::uint64_t coordinate_value(
    const RiskLatticeElement& value,
    std::string_view namespace_id,
    std::string_view component_id) noexcept;
[[nodiscard]] RiskVector risk_vector_from_lattice(const RiskLatticeElement& value) noexcept;
[[nodiscard]] RiskLatticeElement risk_lattice_from_vector(RiskVector value);
[[nodiscard]] Hash256 risk_lattice_hash(RiskLatticeElement value);

}  // namespace pv
