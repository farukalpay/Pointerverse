// SPDX-License-Identifier: Apache-2.0
#include "pv/measure/measurement_store.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <utility>

#include "pv/kernel/canonical_codec.hpp"
#include "pv/measure/risk_lattice.hpp"
#include "pv/storage/repository.hpp"

namespace pv {
namespace {

std::string commit_key(CommitId id) {
    return to_hex(id.value);
}

std::vector<std::byte> encode_bytes(const MeasurementSpec& spec) {
    return canonical_encode(spec);
}

std::vector<std::byte> encode_bytes(const RiskEvidence& evidence) {
    return canonical_encode(evidence);
}

std::vector<std::byte> encode_bytes(const MeasurementRecord& record) {
    return canonical_encode(record);
}

void sort_evidence(std::vector<RiskEvidence>& evidence) {
    std::ranges::sort(evidence, [](const auto& left, const auto& right) {
        const auto left_key = left.component + ":" + to_hex(risk_evidence_hash(left));
        const auto right_key = right.component + ":" + to_hex(risk_evidence_hash(right));
        return left_key < right_key;
    });
}

MeasurementIndexEntry index_entry_for(std::string_view branch, const MeasurementRecord& record) {
    return MeasurementIndexEntry{
        std::string{branch},
        record.commit,
        record.spec_hash,
        record.measurement_hash,
        record.risk,
        record.projection
    };
}

}  // namespace

MeasurementStore::MeasurementStore(Repository& repository)
    : repository_(repository), index_(repository.root()) {}

Hash256 MeasurementStore::put_spec(const MeasurementSpec& spec) const {
    const auto bytes = encode_bytes(spec);
    const auto id = repository_.objects().put_bytes(bytes);
    const auto expected = measurement_spec_hash(spec);
    if (id != expected) {
        throw std::runtime_error("stored measurement spec hash mismatch");
    }
    return id;
}

MeasurementSpec MeasurementStore::load_spec(Hash256 spec_hash) const {
    return decode_measurement_spec_bytes(repository_.objects().get_bytes(spec_hash));
}

MeasurementRecord MeasurementStore::load_record(Hash256 measurement_object) const {
    auto record = decode_measurement_record_bytes(repository_.objects().get_bytes(measurement_object));
    if (record.measurement_hash != measurement_object) {
        throw std::runtime_error("measurement object hash mismatch");
    }
    return record;
}

std::optional<MeasurementLoadResult> MeasurementStore::load_cached(
    std::string_view branch,
    CommitId commit,
    Hash256 spec_hash) const {
    try {
        const auto entry = index_.find(branch, commit, spec_hash);
        if (!entry.has_value()) {
            return std::nullopt;
        }

        auto record = load_record(entry->measurement_object);
        if (record.commit != commit
            || record.spec_hash != spec_hash
            || record.risk != entry->risk
            || record.projection != entry->projection
            || record.measurement_hash != entry->measurement_object
            || record.evidence_root != measurement_evidence_root(record.evidence_objects)) {
            return std::nullopt;
        }

        const auto commit_record = repository_.backend().commit_record(commit);
        if (record.commit_root != commit_record.after_root) {
            return std::nullopt;
        }

        std::vector<RiskEvidence> evidence;
        evidence.reserve(record.evidence_objects.size());
        for (const auto evidence_object : record.evidence_objects) {
            auto item = decode_risk_evidence_bytes(repository_.objects().get_bytes(evidence_object));
            if (risk_evidence_hash(item) != evidence_object) {
                return std::nullopt;
            }
            evidence.push_back(std::move(item));
        }
        sort_evidence(evidence);

        MeasuredRisk measured;
        measured.commit = record.commit;
        measured.commit_root = record.commit_root;
        measured.spec_hash = record.spec_hash;
        measured.value = record.risk;
        measured.projection = record.projection;
        measured.evidence = std::move(evidence);
        measured.evidence_root = record.evidence_root;
        measured.measurement_object = record.measurement_hash;
        measured.measurement_hash = record.measurement_hash;
        return MeasurementLoadResult{std::move(measured), std::move(record), true};
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

MeasurementLoadResult MeasurementStore::compute_and_store(
    std::string_view branch,
    CommitId commit,
    const MeasurementSpec& spec,
    const Verifier* verifier) const {
    const auto spec_hash = put_spec(spec);
    auto measured = MeasuredRiskFunctional{}.measure_commit(repository_, branch, commit, spec, verifier);
    if (measured.spec_hash != spec_hash) {
        throw std::runtime_error("measurement spec hash mismatch");
    }

    std::vector<Hash256> evidence_objects;
    evidence_objects.reserve(measured.evidence.size());
    for (const auto& evidence : measured.evidence) {
        const auto expected = risk_evidence_hash(evidence);
        const auto object = repository_.objects().put_bytes(encode_bytes(evidence));
        if (object != expected) {
            throw std::runtime_error("stored measurement evidence hash mismatch");
        }
        evidence_objects.push_back(object);
    }

    auto record = make_measurement_record(
        measured.commit,
        measured.commit_root,
        measured.spec_hash,
        measured.value,
        measured.projection,
        std::move(evidence_objects));
    const auto object = repository_.objects().put_bytes(encode_bytes(record));
    if (object != record.measurement_hash) {
        throw std::runtime_error("stored measurement record hash mismatch");
    }

    measured.evidence_root = record.evidence_root;
    measured.measurement_object = object;
    measured.measurement_hash = object;
    index_.upsert(branch, index_entry_for(branch, record));
    return MeasurementLoadResult{std::move(measured), std::move(record), false};
}

MeasurementLoadResult MeasurementStore::measure_or_load_commit(
    std::string_view branch,
    CommitId commit,
    const MeasurementSpec& spec,
    const Verifier* verifier) const {
    const auto spec_hash = put_spec(spec);
    if (auto cached = load_cached(branch, commit, spec_hash); cached.has_value()) {
        return *cached;
    }
    return compute_and_store(branch, commit, spec, verifier);
}

MeasurementBranchResult MeasurementStore::measure_or_load_branch(
    std::string_view branch,
    const MeasurementSpec& spec,
    const Verifier* verifier) const {
    MeasurementBranchResult result;
    for (const auto& record : repository_.backend().history(branch)) {
        if (record.origin == TransactionOrigin::Internal) {
            continue;
        }
        auto item = measure_or_load_commit(branch, record.id, spec, verifier);
        if (item.cache_hit) {
            result.cache_hits += 1;
        } else {
            result.cache_misses += 1;
        }
        result.measured.push_back(std::move(item.measured));
        result.records.push_back(std::move(item.record));
    }
    return result;
}

MeasurementBranchResult MeasurementStore::measure_new_commits(
    std::string_view branch,
    const std::vector<CommitId>& existing_history,
    const MeasurementSpec& spec,
    const Verifier* verifier) const {
    std::set<std::string> existing;
    for (const auto id : existing_history) {
        existing.insert(commit_key(id));
    }

    MeasurementBranchResult result;
    for (const auto& record : repository_.backend().history(branch)) {
        if (record.origin == TransactionOrigin::Internal || existing.contains(commit_key(record.id))) {
            continue;
        }
        auto item = measure_or_load_commit(branch, record.id, spec, verifier);
        if (item.cache_hit) {
            result.cache_hits += 1;
        } else {
            result.cache_misses += 1;
        }
        result.measured.push_back(std::move(item.measured));
        result.records.push_back(std::move(item.record));
    }
    return result;
}

MeasurementBranchResult MeasurementStore::rebuild_cache(
    std::string_view branch,
    const MeasurementSpec& spec,
    const Verifier* verifier) const {
    const auto spec_hash = put_spec(spec);
    index_.remove_branch_spec(branch, spec_hash);
    return measure_or_load_branch(branch, spec, verifier);
}

const MeasurementIndex& MeasurementStore::index() const noexcept {
    return index_;
}

RiskVector joined_risk(const MeasurementBranchResult& result) noexcept {
    return joined_risk(result.measured);
}

}  // namespace pv
