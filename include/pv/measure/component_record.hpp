// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "pv/hash/canonical.hpp"

namespace pv {

class CanonicalReader;
class CanonicalWriter;

struct MeasurementComponentRecord {
    std::string namespace_id;
    std::string functional_id;
    Hash256 input_root;
    Hash256 output_root;
    std::uint64_t value{0};
    Hash256 evidence_root;
    Hash256 component_hash;

    friend bool operator==(const MeasurementComponentRecord&, const MeasurementComponentRecord&) = default;
};

[[nodiscard]] Hash256 measurement_component_root(std::vector<Hash256> component_objects);
[[nodiscard]] Hash256 measurement_component_hash(const MeasurementComponentRecord& record);
[[nodiscard]] MeasurementComponentRecord make_measurement_component_record(
    std::string namespace_id,
    std::string functional_id,
    Hash256 input_root,
    Hash256 output_root,
    std::uint64_t value,
    Hash256 evidence_root);

void encode(CanonicalWriter& writer, const MeasurementComponentRecord& record);
[[nodiscard]] MeasurementComponentRecord decode_measurement_component_record(CanonicalReader& reader);
[[nodiscard]] MeasurementComponentRecord decode_measurement_component_record_bytes(std::span<const std::byte> bytes);

}  // namespace pv
