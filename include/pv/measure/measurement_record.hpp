// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "pv/hash/canonical.hpp"
#include "pv/measure/component_record.hpp"
#include "pv/measure/risk_value.hpp"
#include "pv/runtime/ids.hpp"

namespace pv {

class CanonicalReader;
class CanonicalWriter;

struct MeasurementRecord {
    Hash256 id;
    CommitId commit;
    Hash256 commit_root;
    Hash256 spec_hash;
    RiskVector risk;
    std::uint64_t projection{0};
    std::vector<Hash256> component_objects;
    Hash256 component_root;
    std::vector<Hash256> evidence_objects;
    Hash256 evidence_root;
    Hash256 measurement_identity_hash;
    Hash256 measurement_object_hash;
    Hash256 measurement_hash;
    bool legacy{false};

    friend bool operator==(const MeasurementRecord&, const MeasurementRecord&) = default;
};

[[nodiscard]] Hash256 measurement_evidence_root(std::vector<Hash256> evidence_objects);
[[nodiscard]] Hash256 measurement_identity_hash(const MeasurementRecord& record);
[[nodiscard]] Hash256 measurement_record_hash(const MeasurementRecord& record);
[[nodiscard]] MeasurementRecord make_measurement_record(
    CommitId commit,
    Hash256 commit_root,
    Hash256 spec_hash,
    std::vector<Hash256> component_objects,
    std::vector<Hash256> evidence_objects);

void encode(CanonicalWriter& writer, const MeasurementRecord& record);
[[nodiscard]] MeasurementRecord decode_measurement_record(CanonicalReader& reader);
[[nodiscard]] MeasurementRecord decode_measurement_record_bytes(std::span<const std::byte> bytes);

}  // namespace pv
