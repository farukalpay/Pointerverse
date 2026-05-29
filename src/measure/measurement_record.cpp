// SPDX-License-Identifier: Apache-2.0
#include "pv/measure/measurement_record.hpp"

#include <algorithm>
#include <stdexcept>
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

void encode_body(CanonicalWriter& writer, const MeasurementRecord& record) {
    writer.string("MeasurementRecord:v3");
    writer.hash(record.commit.value);
    writer.hash(record.commit_root);
    writer.hash(record.spec_hash);
    auto component_objects = record.component_objects;
    sort_hashes(component_objects);
    writer.u64(component_objects.size());
    for (const auto hash : component_objects) {
        writer.hash(hash);
    }
    writer.hash(record.component_root);
    auto evidence_objects = record.evidence_objects;
    sort_hashes(evidence_objects);
    writer.u64(evidence_objects.size());
    for (const auto hash : evidence_objects) {
        writer.hash(hash);
    }
    writer.hash(record.evidence_root);
}

}  // namespace

Hash256 measurement_evidence_root(std::vector<Hash256> evidence_objects) {
    sort_hashes(evidence_objects);
    CanonicalWriter writer;
    writer.string("MeasurementEvidenceRoot:v1");
    writer.u64(evidence_objects.size());
    for (const auto hash : evidence_objects) {
        writer.hash(hash);
    }
    return sha256(writer.bytes());
}

Hash256 measurement_identity_hash(const MeasurementRecord& record) {
    CanonicalWriter writer;
    writer.string("MeasurementIdentity:v1");
    writer.hash(record.commit.value);
    writer.hash(record.commit_root);
    writer.hash(record.spec_hash);
    writer.hash(record.component_root);
    writer.hash(record.evidence_root);
    return sha256(writer.bytes());
}

void encode(CanonicalWriter& writer, const MeasurementRecord& record) {
    encode_body(writer, record);
}

MeasurementRecord decode_measurement_record(CanonicalReader& reader) {
    const auto tag = reader.string();
    if (tag != "MeasurementRecord:v1" && tag != "MeasurementRecord:v2" && tag != "MeasurementRecord:v3") {
        throw std::runtime_error("canonical stream has unexpected type tag");
    }
    MeasurementRecord record;
    record.commit = CommitId{reader.hash()};
    record.commit_root = reader.hash();
    record.spec_hash = reader.hash();
    if (tag == "MeasurementRecord:v3") {
        const auto component_count = reader.u64();
        record.component_objects.reserve(static_cast<std::size_t>(component_count));
        for (std::uint64_t index = 0; index < component_count; ++index) {
            record.component_objects.push_back(reader.hash());
        }
        record.component_root = reader.hash();
    } else {
        record.legacy = true;
        record.risk.structural = reader.u64();
        record.risk.law_distance = reader.u64();
        record.risk.repair_distance = reader.u64();
        record.risk.surprise = reader.u64();
        if (tag == "MeasurementRecord:v1") {
            record.projection = reader.u64();
        }
    }
    const auto evidence_count = reader.u64();
    record.evidence_objects.reserve(static_cast<std::size_t>(evidence_count));
    for (std::uint64_t index = 0; index < evidence_count; ++index) {
        record.evidence_objects.push_back(reader.hash());
    }
    record.evidence_root = reader.hash();
    record.measurement_identity_hash = record.legacy ? Hash256{} : measurement_identity_hash(record);
    record.measurement_object_hash = measurement_record_hash(record);
    record.id = record.measurement_object_hash;
    record.measurement_hash = record.legacy ? record.measurement_object_hash : record.measurement_identity_hash;
    return record;
}

MeasurementRecord decode_measurement_record_bytes(std::span<const std::byte> bytes) {
    CanonicalReader reader{bytes};
    auto record = decode_measurement_record(reader);
    reader.expect_end();
    const auto id = sha256(bytes);
    record.id = id;
    record.measurement_object_hash = id;
    if (record.legacy) {
        record.measurement_hash = id;
    } else {
        record.measurement_identity_hash = measurement_identity_hash(record);
        record.measurement_hash = record.measurement_identity_hash;
    }
    return record;
}

Hash256 measurement_record_hash(const MeasurementRecord& record) {
    CanonicalWriter writer;
    encode_body(writer, record);
    return sha256(writer.bytes());
}

MeasurementRecord make_measurement_record(
    CommitId commit,
    Hash256 commit_root,
    Hash256 spec_hash,
    std::vector<Hash256> component_objects,
    std::vector<Hash256> evidence_objects) {
    sort_hashes(component_objects);
    sort_hashes(evidence_objects);
    MeasurementRecord record;
    record.commit = commit;
    record.commit_root = commit_root;
    record.spec_hash = spec_hash;
    record.component_objects = std::move(component_objects);
    record.component_root = measurement_component_root(record.component_objects);
    record.evidence_objects = std::move(evidence_objects);
    record.evidence_root = measurement_evidence_root(record.evidence_objects);
    record.measurement_identity_hash = measurement_identity_hash(record);
    record.measurement_object_hash = measurement_record_hash(record);
    record.id = record.measurement_object_hash;
    record.measurement_hash = record.measurement_identity_hash;
    return record;
}

}  // namespace pv
