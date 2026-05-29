// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pv/hash/canonical.hpp"
#include "pv/intervention/operator.hpp"

namespace pv {

class CanonicalWriter;

struct InterventionProgram {
    std::vector<InterventionOperator> operators;
    std::uint64_t canonical_cost{0};
    Hash256 canonical_hash;

    friend bool operator==(const InterventionProgram&, const InterventionProgram&) = default;
};

[[nodiscard]] InterventionProgram identity_intervention_program();
[[nodiscard]] InterventionProgram canonicalize_intervention_program(InterventionProgram program);
[[nodiscard]] InterventionProgram make_intervention_program(std::vector<InterventionOperator> operators);
[[nodiscard]] Hash256 intervention_program_hash(const InterventionProgram& program);
[[nodiscard]] std::string intervention_program_id(const InterventionProgram& program);
[[nodiscard]] bool equivalent_program(const InterventionProgram& left, const InterventionProgram& right) noexcept;

void encode(CanonicalWriter& writer, const InterventionProgram& program);

}  // namespace pv
