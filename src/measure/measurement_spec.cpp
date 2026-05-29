// SPDX-License-Identifier: Apache-2.0
#include "pv/measure/measurement_spec.hpp"

#include "pv/hash/hasher.hpp"
#include "pv/kernel/canonical_codec.hpp"

namespace pv {

MeasurementSpec default_measurement_spec() {
    return MeasurementSpec{};
}

MeasurementSpec agent_audit_measurement_spec() {
    auto spec = default_measurement_spec();
    spec.verifier_id = "agent_audit:v1";
    return spec;
}

bool operator==(const MeasurementSpec& left, const MeasurementSpec& right) noexcept {
    return left.id == right.id
        && left.version == right.version
        && left.structural == right.structural
        && left.law == right.law
        && left.repair == right.repair
        && left.surprise == right.surprise
        && left.repair_options.max_depth == right.repair_options.max_depth
        && left.repair_options.max_candidates == right.repair_options.max_candidates
        && left.projection.structural_weight == right.projection.structural_weight
        && left.projection.law_weight == right.projection.law_weight
        && left.projection.repair_weight == right.projection.repair_weight
        && left.projection.surprise_weight == right.projection.surprise_weight
        && left.verifier_id == right.verifier_id;
}

void encode(CanonicalWriter& writer, const MeasurementSpec& spec) {
    writer.string("MeasurementSpec:v1");
    writer.string(spec.id);
    writer.u32(spec.version);
    writer.u8(spec.structural ? 1U : 0U);
    writer.u8(spec.law ? 1U : 0U);
    writer.u8(spec.repair ? 1U : 0U);
    writer.u8(spec.surprise ? 1U : 0U);
    writer.u32(spec.repair_options.max_depth);
    writer.u32(spec.repair_options.max_candidates);
    writer.u64(spec.projection.structural_weight);
    writer.u64(spec.projection.law_weight);
    writer.u64(spec.projection.repair_weight);
    writer.u64(spec.projection.surprise_weight);
    writer.string(spec.verifier_id);
}

MeasurementSpec decode_measurement_spec(CanonicalReader& reader) {
    reader.expect_tag("MeasurementSpec:v1");
    MeasurementSpec spec;
    spec.id = reader.string();
    spec.version = reader.u32();
    spec.structural = reader.u8() != 0;
    spec.law = reader.u8() != 0;
    spec.repair = reader.u8() != 0;
    spec.surprise = reader.u8() != 0;
    spec.repair_options.max_depth = reader.u32();
    spec.repair_options.max_candidates = reader.u32();
    spec.projection.structural_weight = reader.u64();
    spec.projection.law_weight = reader.u64();
    spec.projection.repair_weight = reader.u64();
    spec.projection.surprise_weight = reader.u64();
    spec.verifier_id = reader.string();
    return spec;
}

MeasurementSpec decode_measurement_spec_bytes(std::span<const std::byte> bytes) {
    CanonicalReader reader{bytes};
    auto spec = decode_measurement_spec(reader);
    reader.expect_end();
    return spec;
}

Hash256 measurement_spec_hash(const MeasurementSpec& spec) {
    return sha256(canonical_encode(spec));
}

}  // namespace pv
