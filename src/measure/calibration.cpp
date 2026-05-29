// SPDX-License-Identifier: Apache-2.0
#include "pv/measure/calibration.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "pv/hash/hasher.hpp"
#include "pv/kernel/canonical_codec.hpp"
#include "pv/measure/measurement_store.hpp"
#include "pv/storage/repository.hpp"

namespace pv {
namespace {

void write_commit_id(CanonicalWriter& writer, CommitId id) {
    writer.hash(id.value);
}

CommitId read_commit_id(CanonicalReader& reader) {
    return CommitId{reader.hash()};
}

void write_index_entry(CanonicalWriter& writer, const MeasurementIndexEntry& entry) {
    writer.string(entry.branch);
    write_commit_id(writer, entry.commit);
    writer.hash(entry.spec_hash);
    writer.hash(entry.measurement_object);
    writer.u64(entry.risk.structural);
    writer.u64(entry.risk.law_distance);
    writer.u64(entry.risk.repair_distance);
    writer.u64(entry.risk.surprise);
    writer.u64(entry.projection);
}

MeasurementIndexEntry read_index_entry(CanonicalReader& reader) {
    MeasurementIndexEntry entry;
    entry.branch = reader.string();
    entry.commit = read_commit_id(reader);
    entry.spec_hash = reader.hash();
    entry.measurement_object = reader.hash();
    entry.risk.structural = reader.u64();
    entry.risk.law_distance = reader.u64();
    entry.risk.repair_distance = reader.u64();
    entry.risk.surprise = reader.u64();
    entry.projection = reader.u64();
    return entry;
}

void write_ref(IndexPayloadWriter& writer, const CalibrationBaselineRef& ref) {
    writer.string(ref.name);
    writer.hash(ref.baseline_object);
}

CalibrationBaselineRef read_ref(IndexPayloadReader& reader) {
    CalibrationBaselineRef ref;
    ref.name = reader.string();
    ref.baseline_object = reader.hash();
    return ref;
}

void sort_refs(std::vector<CalibrationBaselineRef>& refs) {
    std::ranges::sort(refs, {}, &CalibrationBaselineRef::name);
    refs.erase(std::ranges::unique(refs, {}, &CalibrationBaselineRef::name).begin(), refs.end());
}

}  // namespace

void encode(CanonicalWriter& writer, const CalibrationBaseline& baseline) {
    writer.string("CalibrationBaseline:v1");
    writer.string(baseline.branch);
    write_commit_id(writer, baseline.up_to_commit);
    writer.hash(baseline.spec_hash);
    writer.u64(baseline.sample.size());
    for (const auto& entry : baseline.sample) {
        write_index_entry(writer, entry);
    }
}

CalibrationBaseline decode_calibration_baseline(CanonicalReader& reader) {
    reader.expect_tag("CalibrationBaseline:v1");
    CalibrationBaseline baseline;
    baseline.branch = reader.string();
    baseline.up_to_commit = read_commit_id(reader);
    baseline.spec_hash = reader.hash();
    const auto sample_size = reader.u64();
    baseline.sample.reserve(static_cast<std::size_t>(sample_size));
    for (std::uint64_t index = 0; index < sample_size; ++index) {
        baseline.sample.push_back(read_index_entry(reader));
    }
    baseline.baseline_hash = calibration_baseline_hash(baseline);
    return baseline;
}

CalibrationBaseline decode_calibration_baseline_bytes(std::span<const std::byte> bytes) {
    CanonicalReader reader{bytes};
    auto baseline = decode_calibration_baseline(reader);
    reader.expect_end();
    baseline.baseline_hash = sha256(bytes);
    return baseline;
}

Hash256 calibration_baseline_hash(const CalibrationBaseline& baseline) {
    return sha256(canonical_encode(baseline));
}

CalibrationStore::CalibrationStore(Repository& repository)
    : repository_(repository), refs_(repository.root(), "measurement_baselines.idx", "PVCALBASEIDX1") {}

std::vector<CalibrationBaselineRef> CalibrationStore::refs() const {
    if (!refs_.exists()) {
        return {};
    }
    const auto payload = refs_.read_payload();
    IndexPayloadReader reader{payload};
    const auto size = reader.u64();
    std::vector<CalibrationBaselineRef> out;
    out.reserve(static_cast<std::size_t>(size));
    for (std::uint64_t index = 0; index < size; ++index) {
        out.push_back(read_ref(reader));
    }
    reader.expect_end();
    return out;
}

void CalibrationStore::write_refs(std::vector<CalibrationBaselineRef> refs) const {
    sort_refs(refs);
    IndexPayloadWriter writer;
    writer.u64(refs.size());
    for (const auto& ref : refs) {
        write_ref(writer, ref);
    }
    refs_.write_payload(writer.bytes());
}

void CalibrationStore::save_ref(std::string_view name, Hash256 baseline_object) const {
    auto all = refs();
    auto iter = std::ranges::find(all, name, &CalibrationBaselineRef::name);
    if (iter == all.end()) {
        all.push_back(CalibrationBaselineRef{std::string{name}, baseline_object});
    } else {
        iter->baseline_object = baseline_object;
    }
    write_refs(std::move(all));
}

std::optional<CalibrationBaselineRef> CalibrationStore::find_ref(std::string_view name) const {
    for (const auto& ref : refs()) {
        if (ref.name == name) {
            return ref;
        }
    }
    return std::nullopt;
}

CalibrationBaseline CalibrationStore::load(std::string_view name) const {
    const auto ref = find_ref(name);
    if (!ref.has_value()) {
        throw std::out_of_range("unknown calibration baseline '" + std::string{name} + "'");
    }
    return load(ref->baseline_object);
}

CalibrationBaseline CalibrationStore::load(Hash256 baseline_object) const {
    auto baseline = decode_calibration_baseline_bytes(repository_.objects().get_bytes(baseline_object));
    if (baseline.baseline_hash != baseline_object) {
        throw std::runtime_error("calibration baseline object hash mismatch");
    }
    return baseline;
}

CalibrationBaseline CalibrationStore::create(
    std::string_view name,
    std::string_view branch,
    CommitId up_to_commit,
    const MeasurementSpec& spec,
    const Verifier* verifier) const {
    MeasurementStore measurements{repository_};
    const auto spec_hash = measurements.put_spec(spec);
    CalibrationBaseline baseline;
    baseline.branch = std::string{branch};
    baseline.up_to_commit = up_to_commit;
    baseline.spec_hash = spec_hash;

    bool found = false;
    for (const auto& record : repository_.backend().history(branch)) {
        if (record.origin == TransactionOrigin::Internal) {
            continue;
        }
        auto measured = measurements.measure_or_load_commit(branch, record.id, spec, verifier);
        baseline.sample.push_back(MeasurementIndexEntry{
            std::string{branch},
            measured.record.commit,
            measured.record.spec_hash,
            measured.record.measurement_hash,
            measured.record.risk,
            measured.record.projection
        });
        if (record.id == up_to_commit) {
            found = true;
            break;
        }
    }
    if (!found) {
        throw std::out_of_range("baseline up-to commit was not found on branch");
    }

    baseline.baseline_hash = calibration_baseline_hash(baseline);
    const auto object = repository_.objects().put_bytes(canonical_encode(baseline));
    if (object != baseline.baseline_hash) {
        throw std::runtime_error("stored calibration baseline hash mismatch");
    }
    save_ref(name, object);
    return baseline;
}

bool calibration_contains_commit(const CalibrationBaseline& baseline, CommitId commit) noexcept {
    return std::ranges::any_of(baseline.sample, [&](const auto& entry) {
        return entry.commit == commit;
    });
}

}  // namespace pv
