// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "pv/hash/canonical.hpp"
#include "pv/measure/repair_measure.hpp"
#include "pv/measure/risk_projection.hpp"

namespace pv {

class CanonicalReader;
class CanonicalWriter;

struct MeasurementSpec {
    std::string id{"pointerverse.measured_risk"};
    std::uint32_t version{1};
    bool structural{true};
    bool law{true};
    bool repair{true};
    bool surprise{true};
    RepairSearchOptions repair_options;
    RiskProjection projection;
    std::string verifier_id{"none"};

    friend bool operator==(const MeasurementSpec& left, const MeasurementSpec& right) noexcept;
};

[[nodiscard]] MeasurementSpec default_measurement_spec();
[[nodiscard]] MeasurementSpec agent_audit_measurement_spec();
[[nodiscard]] Hash256 measurement_spec_hash(const MeasurementSpec& spec);

void encode(CanonicalWriter& writer, const MeasurementSpec& spec);
[[nodiscard]] MeasurementSpec decode_measurement_spec(CanonicalReader& reader);
[[nodiscard]] MeasurementSpec decode_measurement_spec_bytes(std::span<const std::byte> bytes);

}  // namespace pv
