// SPDX-License-Identifier: Apache-2.0
#include "pv/measure/component_record.hpp"

#include <algorithm>
#include <utility>

#include "pv/hash/hasher.hpp"
#include "pv/kernel/canonical_codec.hpp"

namespace pv {
namespace {

void sort_hashes(std::vector<Hash256>& hashes) {
    std::ranges::sort(hashes, [](Hash256 left, Hash256 right) {
        return to_hex(left) < to_hex(right);
    });
    hashes.erase(std::ranges::unique(hashes).begin(), hashes.end());
}

void encode_body(CanonicalWriter& writer, const MeasurementComponentRecord& record) {
    writer.string("MeasurementComponentRecord:v1");
    writer.string(record.namespace_id);
    writer.string(record.functional_id);
    writer.hash(record.input_root);
    writer.hash(record.output_root);
    writer.u64(record.value);
    writer.hash(record.evidence_root);
}

}  // namespace

Hash256 measurement_component_root(std::vector<Hash256> component_objects) {
    sort_hashes(component_objects);
    CanonicalWriter writer;
    writer.string("MeasurementComponentRoot:v1");
    writer.u64(component_objects.size());
    for (const auto hash : component_objects) {
        writer.hash(hash);
    }
    return sha256(writer.bytes());
}

Hash256 measurement_component_hash(const MeasurementComponentRecord& record) {
    CanonicalWriter writer;
    encode_body(writer, record);
    return sha256(writer.bytes());
}

MeasurementComponentRecord make_measurement_component_record(
    std::string namespace_id,
    std::string functional_id,
    Hash256 input_root,
    Hash256 output_root,
    std::uint64_t value,
    Hash256 evidence_root) {
    MeasurementComponentRecord record;
    record.namespace_id = std::move(namespace_id);
    record.functional_id = std::move(functional_id);
    record.input_root = input_root;
    record.output_root = output_root;
    record.value = value;
    record.evidence_root = evidence_root;
    record.component_hash = measurement_component_hash(record);
    return record;
}

void encode(CanonicalWriter& writer, const MeasurementComponentRecord& record) {
    encode_body(writer, record);
}

MeasurementComponentRecord decode_measurement_component_record(CanonicalReader& reader) {
    reader.expect_tag("MeasurementComponentRecord:v1");
    MeasurementComponentRecord record;
    record.namespace_id = reader.string();
    record.functional_id = reader.string();
    record.input_root = reader.hash();
    record.output_root = reader.hash();
    record.value = reader.u64();
    record.evidence_root = reader.hash();
    record.component_hash = measurement_component_hash(record);
    return record;
}

MeasurementComponentRecord decode_measurement_component_record_bytes(std::span<const std::byte> bytes) {
    CanonicalReader reader{bytes};
    auto record = decode_measurement_component_record(reader);
    reader.expect_end();
    record.component_hash = sha256(bytes);
    return record;
}

}  // namespace pv
