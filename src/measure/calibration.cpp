// SPDX-License-Identifier: Apache-2.0
#include "pv/measure/calibration.hpp"

#include <algorithm>
#include <map>
#include <cmath>
#include <stdexcept>
#include <utility>

#include "pv/hash/hasher.hpp"
#include "pv/kernel/canonical_codec.hpp"
#include "pv/measure/component_record.hpp"
#include "pv/measure/measurement_record.hpp"
#include "pv/measure/measurement_store.hpp"
#include "pv/measure/risk_projection.hpp"
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
    writer.hash(entry.measurement_identity_hash);
    writer.hash(entry.component_root);
    writer.hash(entry.evidence_root);
    writer.u64(entry.risk.structural);
    writer.u64(entry.risk.law_distance);
    writer.u64(entry.risk.repair_distance);
    writer.u64(entry.risk.surprise);
    writer.u64(entry.projection);
    writer.u8(entry.needs_rebuild ? 1U : 0U);
}

MeasurementIndexEntry read_index_entry(CanonicalReader& reader, bool v2) {
    MeasurementIndexEntry entry;
    entry.branch = reader.string();
    entry.commit = read_commit_id(reader);
    entry.spec_hash = reader.hash();
    entry.measurement_object = reader.hash();
    if (v2) {
        entry.measurement_identity_hash = reader.hash();
        entry.component_root = reader.hash();
        entry.evidence_root = reader.hash();
    } else {
        entry.measurement_identity_hash = entry.measurement_object;
        entry.needs_rebuild = true;
    }
    entry.risk.structural = reader.u64();
    entry.risk.law_distance = reader.u64();
    entry.risk.repair_distance = reader.u64();
    entry.risk.surprise = reader.u64();
    entry.projection = reader.u64();
    if (v2) {
        entry.needs_rebuild = reader.u8() != 0;
    }
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

bool stats_less(const ComponentStats& left, const ComponentStats& right) {
    if (left.namespace_id != right.namespace_id) {
        return left.namespace_id < right.namespace_id;
    }
    return left.component_id < right.component_id;
}

void sort_stats(std::vector<ComponentStats>& stats) {
    std::ranges::sort(stats, stats_less);
}

std::uint64_t percentile(std::vector<std::uint64_t> values, double p) {
    if (values.empty()) {
        return 0;
    }
    std::ranges::sort(values);
    if (values.size() == 1) {
        return values.front();
    }
    const auto index = static_cast<std::size_t>(
        std::ceil(std::clamp(p, 0.0, 1.0) * static_cast<double>(values.size() - 1U)));
    return values[std::min(index, values.size() - 1U)];
}

ComponentStats stats_for(
    std::string namespace_id,
    std::string component_id,
    const std::vector<std::uint64_t>& values) {
    const auto median = percentile(values, 0.50);
    std::vector<std::uint64_t> deviations;
    deviations.reserve(values.size());
    for (const auto value : values) {
        deviations.push_back(value > median ? value - median : median - value);
    }
    auto sorted = values;
    std::ranges::sort(sorted);
    return ComponentStats{
        std::move(namespace_id),
        std::move(component_id),
        median,
        std::max<std::uint64_t>(1, percentile(deviations, 0.50)),
        percentile(values, 0.80),
        percentile(values, 0.95),
        percentile(values, 0.99),
        sorted.empty() ? 0 : sorted.back()
    };
}

void write_stats(CanonicalWriter& writer, const ComponentStats& stats) {
    writer.string(stats.namespace_id);
    writer.string(stats.component_id);
    writer.u64(stats.median);
    writer.u64(stats.mad);
    writer.u64(stats.q80);
    writer.u64(stats.q95);
    writer.u64(stats.q99);
    writer.u64(stats.max_seen);
}

ComponentStats read_stats(CanonicalReader& reader) {
    ComponentStats stats;
    stats.namespace_id = reader.string();
    stats.component_id = reader.string();
    stats.median = reader.u64();
    stats.mad = reader.u64();
    stats.q80 = reader.u64();
    stats.q95 = reader.u64();
    stats.q99 = reader.u64();
    stats.max_seen = reader.u64();
    return stats;
}

}  // namespace

void encode(CanonicalWriter& writer, const CalibrationBaseline& baseline) {
    writer.string("CalibrationBaseline:v2");
    writer.string(baseline.branch);
    write_commit_id(writer, baseline.up_to_commit);
    writer.hash(baseline.spec_hash);
    writer.u64(baseline.sample.size());
    for (const auto& entry : baseline.sample) {
        write_index_entry(writer, entry);
    }
}

CalibrationBaseline decode_calibration_baseline(CanonicalReader& reader) {
    const auto tag = reader.string();
    if (tag != "CalibrationBaseline:v1" && tag != "CalibrationBaseline:v2") {
        throw std::runtime_error("canonical stream has unexpected type tag");
    }
    CalibrationBaseline baseline;
    baseline.branch = reader.string();
    baseline.up_to_commit = read_commit_id(reader);
    baseline.spec_hash = reader.hash();
    const auto sample_size = reader.u64();
    baseline.sample.reserve(static_cast<std::size_t>(sample_size));
    for (std::uint64_t index = 0; index < sample_size; ++index) {
        baseline.sample.push_back(read_index_entry(reader, tag == "CalibrationBaseline:v2"));
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

void encode(CanonicalWriter& writer, const CalibrationProfile& profile) {
    auto sorted = profile.components;
    sort_stats(sorted);
    writer.string("CalibrationProfile:v1");
    writer.hash(profile.baseline_hash);
    writer.u64(sorted.size());
    for (const auto& stats : sorted) {
        write_stats(writer, stats);
    }
}

CalibrationProfile decode_calibration_profile(CanonicalReader& reader) {
    reader.expect_tag("CalibrationProfile:v1");
    CalibrationProfile profile;
    profile.baseline_hash = reader.hash();
    const auto size = reader.u64();
    profile.components.reserve(static_cast<std::size_t>(size));
    for (std::uint64_t index = 0; index < size; ++index) {
        profile.components.push_back(read_stats(reader));
    }
    sort_stats(profile.components);
    profile.profile_hash = calibration_profile_hash(profile);
    return profile;
}

CalibrationProfile decode_calibration_profile_bytes(std::span<const std::byte> bytes) {
    CanonicalReader reader{bytes};
    auto profile = decode_calibration_profile(reader);
    reader.expect_end();
    profile.profile_hash = sha256(bytes);
    return profile;
}

Hash256 calibration_profile_hash(const CalibrationProfile& profile) {
    return sha256(canonical_encode(profile));
}

CalibrationProfile calibration_profile_from_baseline(
    const Repository& repository,
    const CalibrationBaseline& baseline) {
    std::map<std::pair<std::string, std::string>, std::vector<std::uint64_t>> values;
    for (const auto& entry : baseline.sample) {
        try {
            const auto record = decode_measurement_record_bytes(repository.objects().get_bytes(entry.measurement_object));
            if (record.legacy || record.component_objects.empty()) {
                for (const auto& coordinate : risk_lattice_from_vector(entry.risk).coordinates) {
                    values[{coordinate.namespace_id, coordinate.component_id}].push_back(coordinate.value);
                }
                continue;
            }
            for (const auto object : record.component_objects) {
                const auto component = decode_measurement_component_record_bytes(repository.objects().get_bytes(object));
                values[{component.namespace_id, component.functional_id}].push_back(component.value);
            }
        } catch (const std::exception&) {
            for (const auto& coordinate : risk_lattice_from_vector(entry.risk).coordinates) {
                values[{coordinate.namespace_id, coordinate.component_id}].push_back(coordinate.value);
            }
        }
    }

    CalibrationProfile profile;
    profile.baseline_hash = baseline.baseline_hash;
    profile.components.reserve(values.size());
    for (const auto& [key, sample] : values) {
        profile.components.push_back(stats_for(key.first, key.second, sample));
    }
    sort_stats(profile.components);
    profile.profile_hash = calibration_profile_hash(profile);
    return profile;
}

ProjectionPolicy calibrated_projection_policy(
    const CalibrationProfile& profile,
    std::string calibration_mode) {
    ProjectionPolicy policy;
    policy.id = "pointerverse.calibrated_monotone_projection";
    policy.version = 1;
    policy.terms.reserve(profile.components.size());
    for (const auto& stats : profile.components) {
        ProjectionTerm term;
        term.namespace_id = stats.namespace_id;
        term.component_id = stats.component_id;
        term.weight_num = 1;
        term.weight_den = 1;
        term.calibration_mode = calibration_mode;
        term.median = stats.median;
        term.mad = stats.mad;
        term.q80 = stats.q80;
        term.q95 = stats.q95;
        term.q99 = stats.q99;
        policy.terms.push_back(std::move(term));
    }
    return policy;
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
            measured.record.measurement_object_hash,
            measured.record.measurement_identity_hash,
            measured.record.component_root,
            measured.record.evidence_root,
            measured.record.risk,
            measured.record.projection,
            false
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
