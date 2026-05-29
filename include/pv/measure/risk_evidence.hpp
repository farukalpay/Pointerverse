// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "pv/core/id.hpp"
#include "pv/core/pointer.hpp"
#include "pv/hash/canonical.hpp"
#include "pv/law/law.hpp"
#include "pv/runtime/ids.hpp"

namespace pv {

class CanonicalReader;
class CanonicalWriter;

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
    std::string namespace_id;
    std::string functional_id;
    std::string name;
    std::uint64_t value{0};
    RiskEvidence evidence;
};

[[nodiscard]] std::string measured_component_id(const MeasuredComponent& component);
[[nodiscard]] Hash256 risk_evidence_hash(RiskEvidence evidence);
void encode(CanonicalWriter& writer, const RiskEvidence& evidence);
[[nodiscard]] RiskEvidence decode_risk_evidence(CanonicalReader& reader);
[[nodiscard]] RiskEvidence decode_risk_evidence_bytes(std::span<const std::byte> bytes);

}  // namespace pv
