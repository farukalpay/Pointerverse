// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "pv/breakpoint/breakpoint.hpp"
#include "pv/hash/canonical.hpp"
#include "pv/intervention/operator.hpp"

namespace pv {

class ProjectionStore;

struct OperatorFamily {
    std::string id;
    std::string name;
    InterventionKind kind{InterventionKind::Identity};
    RepairCandidate seed;
    Hash256 canonical_hash;
};

[[nodiscard]] Hash256 operator_family_hash(const OperatorFamily& family);
[[nodiscard]] OperatorFamily canonicalize_operator_family(OperatorFamily family);
[[nodiscard]] InterventionOperator make_operator(const OperatorFamily& family, ScaleValue scale);

class OperatorFamilyBuilder {
public:
    [[nodiscard]] std::vector<OperatorFamily> build(
        const ProjectionStore& store,
        std::string_view branch,
        const Breakpoint& breakpoint) const;
};

}  // namespace pv
