// SPDX-License-Identifier: Apache-2.0
#include "pv/measure/risk_projection.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include "pv/hash/hasher.hpp"
#include "pv/kernel/canonical_codec.hpp"

namespace pv {
namespace {

std::uint64_t saturating_mul(std::uint64_t left, std::uint64_t right) noexcept {
    if (left == 0 || right == 0) {
        return 0;
    }
    constexpr auto max = std::numeric_limits<std::uint64_t>::max();
    if (left > max / right) {
        return max;
    }
    return left * right;
}

std::uint64_t saturating_add(std::uint64_t left, std::uint64_t right) noexcept {
    constexpr auto max = std::numeric_limits<std::uint64_t>::max();
    if (max - left < right) {
        return max;
    }
    return left + right;
}

std::uint64_t saturating_mul_div(std::uint64_t value, std::uint64_t numerator, std::uint64_t denominator) noexcept {
    if (denominator == 0) {
        return std::numeric_limits<std::uint64_t>::max();
    }
#if defined(__SIZEOF_INT128__)
    const auto product = static_cast<unsigned __int128>(value) * static_cast<unsigned __int128>(numerator);
    const auto divided = product / denominator;
    if (divided > std::numeric_limits<std::uint64_t>::max()) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return static_cast<std::uint64_t>(divided);
#else
    return saturating_mul(value / denominator, numerator);
#endif
}

bool term_less(const ProjectionTerm& left, const ProjectionTerm& right) {
    if (left.namespace_id != right.namespace_id) {
        return left.namespace_id < right.namespace_id;
    }
    if (left.component_id != right.component_id) {
        return left.component_id < right.component_id;
    }
    if (left.weight_num != right.weight_num) {
        return left.weight_num < right.weight_num;
    }
    if (left.weight_den != right.weight_den) {
        return left.weight_den < right.weight_den;
    }
    if (left.calibration_mode != right.calibration_mode) {
        return left.calibration_mode < right.calibration_mode;
    }
    if (left.median != right.median) {
        return left.median < right.median;
    }
    if (left.mad != right.mad) {
        return left.mad < right.mad;
    }
    if (left.q80 != right.q80) {
        return left.q80 < right.q80;
    }
    if (left.q95 != right.q95) {
        return left.q95 < right.q95;
    }
    return left.q99 < right.q99;
}

void sort_terms(std::vector<ProjectionTerm>& terms) {
    std::ranges::sort(terms, term_less);
}

std::string key(const ProjectionIndexEntry& entry) {
    return to_hex(entry.measurement_hash) + ":" + to_hex(entry.projection_policy_hash);
}

bool same_key(const ProjectionIndexEntry& left, const ProjectionIndexEntry& right) {
    return left.measurement_hash == right.measurement_hash
        && left.projection_policy_hash == right.projection_policy_hash;
}

void sort_entries(std::vector<ProjectionIndexEntry>& entries) {
    std::ranges::sort(entries, [](const auto& left, const auto& right) {
        return key(left) < key(right);
    });
}

std::uint64_t calibrated_coordinate(std::uint64_t coordinate, const ProjectionTerm& term) noexcept {
    if (term.calibration_mode == "raw") {
        return coordinate;
    }
    if (term.calibration_mode == "robust_z") {
        if (coordinate <= term.median) {
            return 0;
        }
        const auto scale = std::max<std::uint64_t>(1, term.mad);
        const auto excess = static_cast<double>(coordinate - term.median) / static_cast<double>(scale);
        const auto normalized = std::ceil(1000.0 * std::log1p(excess));
        if (!std::isfinite(normalized) || normalized <= 0.0) {
            return 0;
        }
        if (normalized >= static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
            return std::numeric_limits<std::uint64_t>::max();
        }
        return static_cast<std::uint64_t>(normalized);
    }
    if (term.calibration_mode == "quantile") {
        if (coordinate < term.q80) {
            return 0;
        }
        if (coordinate < term.q95) {
            return 1000;
        }
        if (coordinate < term.q99) {
            return 2000;
        }
        return 3000;
    }
    return coordinate;
}

}  // namespace

ProjectionPolicy default_projection_policy() {
    ProjectionPolicy policy;
    policy.id = "pointerverse.raw_monotone_projection";
    policy.terms = {
        ProjectionTerm{"law", "total_magnitude", 1, 1},
        ProjectionTerm{"repair", "distance", 1, 1},
        ProjectionTerm{"structural", "forward_cone_mass", 1, 1},
        ProjectionTerm{"structural", "reverse_dependency_mass", 1, 1},
        ProjectionTerm{"structural", "cut_vertex_impact", 1, 1},
        ProjectionTerm{"structural", "boundary_expansion", 1, 1},
        ProjectionTerm{"structural", "path_multiplicity", 1, 1},
        ProjectionTerm{"structural", "propagated_mass", 1, 1},
        ProjectionTerm{"surprise", "history_distance", 1, 1}
    };
    return policy;
}

ProjectionPolicy projection_policy_from_legacy(RiskProjection projection) {
    auto policy = default_projection_policy();
    for (auto& term : policy.terms) {
        if (term.namespace_id == "structural") {
            term.weight_num = saturating_mul(term.weight_num, projection.structural_weight);
        } else if (term.namespace_id == "law") {
            term.weight_num = saturating_mul(term.weight_num, projection.law_weight);
        } else if (term.namespace_id == "repair") {
            term.weight_num = saturating_mul(term.weight_num, projection.repair_weight);
        } else if (term.namespace_id == "surprise") {
            term.weight_num = saturating_mul(term.weight_num, projection.surprise_weight);
        }
    }
    return policy;
}

std::uint64_t project(RiskVector value, RiskProjection projection) noexcept {
    std::uint64_t out = 0;
    out = saturating_add(out, saturating_mul(value.structural, projection.structural_weight));
    out = saturating_add(out, saturating_mul(value.law_distance, projection.law_weight));
    out = saturating_add(out, saturating_mul(value.repair_distance, projection.repair_weight));
    out = saturating_add(out, saturating_mul(value.surprise, projection.surprise_weight));
    return out;
}

std::uint64_t project(const RiskLatticeElement& value, ProjectionPolicy projection) noexcept {
    sort_terms(projection.terms);
    std::uint64_t out = 0;
    for (const auto& term : projection.terms) {
        const auto coordinate = coordinate_value(value, term.namespace_id, term.component_id);
        const auto calibrated = calibrated_coordinate(coordinate, term);
        const auto projected = saturating_mul_div(calibrated, term.weight_num, term.weight_den);
        out = saturating_add(out, projected);
    }
    return out;
}

void encode(CanonicalWriter& writer, const ProjectionPolicy& value) {
    auto policy = value;
    sort_terms(policy.terms);
    writer.string("ProjectionPolicy:v3");
    writer.string(policy.id);
    writer.u32(policy.version);
    writer.u64(policy.terms.size());
    for (const auto& term : policy.terms) {
        writer.string(term.namespace_id);
        writer.string(term.component_id);
        writer.u64(term.weight_num);
        writer.u64(term.weight_den);
        writer.string(term.calibration_mode);
        writer.u64(term.median);
        writer.u64(term.mad);
        writer.u64(term.q80);
        writer.u64(term.q95);
        writer.u64(term.q99);
    }
}

ProjectionPolicy decode_projection_policy(CanonicalReader& reader) {
    const auto tag = reader.string();
    if (tag == "ProjectionPolicy:v1") {
        RiskProjection legacy;
        legacy.structural_weight = reader.u64();
        legacy.law_weight = reader.u64();
        legacy.repair_weight = reader.u64();
        legacy.surprise_weight = reader.u64();
        return projection_policy_from_legacy(legacy);
    }
    if (tag != "ProjectionPolicy:v2" && tag != "ProjectionPolicy:v3") {
        throw std::runtime_error("canonical stream has unexpected type tag");
    }
    ProjectionPolicy policy;
    policy.id = reader.string();
    policy.version = reader.u32();
    const auto size = reader.u64();
    policy.terms.reserve(static_cast<std::size_t>(size));
    for (std::uint64_t index = 0; index < size; ++index) {
        ProjectionTerm term;
        term.namespace_id = reader.string();
        term.component_id = reader.string();
        term.weight_num = reader.u64();
        term.weight_den = reader.u64();
        if (tag == "ProjectionPolicy:v3") {
            term.calibration_mode = reader.string();
            term.median = reader.u64();
            term.mad = reader.u64();
            term.q80 = reader.u64();
            term.q95 = reader.u64();
            term.q99 = reader.u64();
        }
        policy.terms.push_back(std::move(term));
    }
    sort_terms(policy.terms);
    return policy;
}

ProjectionPolicy decode_projection_policy_bytes(std::span<const std::byte> bytes) {
    CanonicalReader reader{bytes};
    auto policy = decode_projection_policy(reader);
    reader.expect_end();
    return policy;
}

Hash256 projection_policy_hash(ProjectionPolicy projection) {
    CanonicalWriter writer;
    encode(writer, projection);
    return sha256(writer.bytes());
}

Hash256 projection_policy_hash(RiskProjection projection) {
    return projection_policy_hash(projection_policy_from_legacy(projection));
}

void encode(CanonicalWriter& writer, const ProjectionRecord& record) {
    writer.string("ProjectionRecord:v1");
    writer.hash(record.measurement_hash);
    writer.hash(record.projection_policy_hash);
    writer.u64(record.projected_score);
    writer.string(record.decision);
    writer.hash(record.baseline_hash);
}

ProjectionRecord decode_projection_record(CanonicalReader& reader) {
    reader.expect_tag("ProjectionRecord:v1");
    ProjectionRecord record;
    record.measurement_hash = reader.hash();
    record.projection_policy_hash = reader.hash();
    record.projected_score = reader.u64();
    record.decision = reader.string();
    record.baseline_hash = reader.hash();
    record.projection_hash = projection_result_hash(record);
    return record;
}

ProjectionRecord decode_projection_record_bytes(std::span<const std::byte> bytes) {
    CanonicalReader reader{bytes};
    auto record = decode_projection_record(reader);
    reader.expect_end();
    record.projection_hash = sha256(bytes);
    return record;
}

Hash256 projection_result_hash(const ProjectionRecord& result) {
    CanonicalWriter writer;
    encode(writer, result);
    return sha256(writer.bytes());
}

ProjectionResult make_projection_result(
    Hash256 measurement_hash,
    RiskVector value,
    RiskProjection projection,
    std::string decision,
    Hash256 baseline_hash) {
    ProjectionResult result;
    result.measurement_hash = measurement_hash;
    result.projection_policy_hash = projection_policy_hash(projection);
    result.projected_score = project(value, projection);
    result.decision = std::move(decision);
    result.baseline_hash = baseline_hash;
    result.projection_hash = projection_result_hash(result);
    return result;
}

ProjectionRecord make_projection_result(
    Hash256 measurement_hash,
    const RiskLatticeElement& value,
    ProjectionPolicy projection,
    std::string decision,
    Hash256 baseline_hash) {
    ProjectionRecord result;
    result.measurement_hash = measurement_hash;
    result.projection_policy_hash = projection_policy_hash(projection);
    result.projected_score = project(value, std::move(projection));
    result.decision = std::move(decision);
    result.baseline_hash = baseline_hash;
    result.projection_hash = projection_result_hash(result);
    return result;
}

ProjectionIndex::ProjectionIndex(std::filesystem::path root)
    : store_(std::move(root), "projections.idx", "PVPROJIDX1") {}

bool ProjectionIndex::exists() const {
    return store_.exists();
}

std::vector<ProjectionIndexEntry> ProjectionIndex::entries() const {
    if (!exists()) {
        return {};
    }
    const auto payload = store_.read_payload();
    IndexPayloadReader reader{payload};
    const auto size = reader.u64();
    std::vector<ProjectionIndexEntry> entries;
    entries.reserve(static_cast<std::size_t>(size));
    for (std::uint64_t index = 0; index < size; ++index) {
        ProjectionIndexEntry entry;
        entry.measurement_hash = reader.hash();
        entry.projection_policy_hash = reader.hash();
        entry.projection_object = reader.hash();
        entry.projected_score = reader.u64();
        entry.decision = reader.string();
        entry.baseline_hash = reader.hash();
        entries.push_back(std::move(entry));
    }
    reader.expect_end();
    return entries;
}

std::optional<ProjectionIndexEntry> ProjectionIndex::find(Hash256 measurement, Hash256 policy) const {
    for (const auto& entry : entries()) {
        if (entry.measurement_hash == measurement && entry.projection_policy_hash == policy) {
            return entry;
        }
    }
    return std::nullopt;
}

IndexFileStatus ProjectionIndex::check() const {
    return store_.check();
}

Hash256 ProjectionIndex::checksum() const {
    return store_.checksum();
}

void ProjectionIndex::write(std::vector<ProjectionIndexEntry> entries) const {
    sort_entries(entries);
    entries.erase(std::unique(entries.begin(), entries.end(), same_key), entries.end());
    IndexPayloadWriter writer;
    writer.u64(entries.size());
    for (const auto& entry : entries) {
        writer.hash(entry.measurement_hash);
        writer.hash(entry.projection_policy_hash);
        writer.hash(entry.projection_object);
        writer.u64(entry.projected_score);
        writer.string(entry.decision);
        writer.hash(entry.baseline_hash);
    }
    store_.write_payload(writer.bytes());
}

void ProjectionIndex::upsert(ProjectionIndexEntry entry) const {
    auto all = entries();
    auto iter = std::ranges::find_if(all, [&](const auto& item) {
        return same_key(item, entry);
    });
    if (iter == all.end()) {
        all.push_back(std::move(entry));
    } else {
        *iter = std::move(entry);
    }
    write(std::move(all));
}

void ProjectionIndex::remove() const {
    store_.remove();
}

}  // namespace pv
