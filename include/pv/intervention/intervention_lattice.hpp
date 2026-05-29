// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "pv/intervention/operator_program.hpp"

namespace pv {

enum class InterventionOrder {
    Weaker,
    Stronger,
    Equivalent,
    Incomparable
};

struct InterventionLatticeRelation {
    InterventionProgram left;
    InterventionProgram right;
    InterventionOrder order{InterventionOrder::Incomparable};
    std::string explanation;
};

[[nodiscard]] std::string_view to_string(InterventionOrder order) noexcept;
[[nodiscard]] InterventionOrder compare_interventions(
    const InterventionProgram& left,
    const InterventionProgram& right) noexcept;
[[nodiscard]] InterventionLatticeRelation intervention_lattice_relation(
    InterventionProgram left,
    InterventionProgram right);

}  // namespace pv
