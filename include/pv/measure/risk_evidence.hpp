// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pv/core/id.hpp"
#include "pv/core/pointer.hpp"
#include "pv/hash/canonical.hpp"
#include "pv/law/law.hpp"
#include "pv/runtime/ids.hpp"

namespace pv {

struct RiskEvidence {
    std::string component;
    Hash256 input_root;
    Hash256 output_root;
    std::vector<ObjectId> objects;
    std::vector<PointerId> pointers;
    std::vector<CommitId> commits;
    std::vector<LawId> laws;
    std::string explanation;
};

struct MeasuredComponent {
    std::string name;
    std::uint64_t value{0};
    RiskEvidence evidence;
};

[[nodiscard]] Hash256 risk_evidence_hash(RiskEvidence evidence);

}  // namespace pv

