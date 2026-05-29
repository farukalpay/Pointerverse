// SPDX-License-Identifier: Apache-2.0
#include "pv/measure/risk_functional.hpp"

#include <algorithm>

#include "pv/hash/hasher.hpp"
#include "pv/kernel/canonical_codec.hpp"
#include "pv/measure/history_measure.hpp"
#include "pv/measure/law_measure.hpp"
#include "pv/measure/measurement_record.hpp"
#include "pv/measure/risk_lattice.hpp"
#include "pv/measure/risk_projection.hpp"
#include "pv/measure/structural_measure.hpp"
#include "pv/storage/repository.hpp"

namespace pv {
namespace {

void add_component(MeasuredRisk& risk, const MeasuredComponent& component) {
    if (component.name == "structural") {
        risk.value.structural = component.value;
    } else if (component.name == "law") {
        risk.value.law_distance = component.value;
    } else if (component.name == "repair") {
        risk.value.repair_distance = component.value;
    } else if (component.name == "surprise") {
        risk.value.surprise = component.value;
    }
    risk.evidence.push_back(component.evidence);
}

std::string evidence_key(const RiskEvidence& evidence) {
    return evidence.component + ":" + to_hex(risk_evidence_hash(evidence));
}

std::vector<Hash256> evidence_hashes(const std::vector<RiskEvidence>& evidence) {
    std::vector<Hash256> out;
    out.reserve(evidence.size());
    for (const auto& item : evidence) {
        out.push_back(risk_evidence_hash(item));
    }
    return out;
}

MeasurementSpec spec_from_options(RepairSearchOptions repair_options) {
    auto spec = default_measurement_spec();
    spec.repair_options = repair_options;
    return spec;
}

}  // namespace

Hash256 measured_risk_hash(
    CommitId commit,
    Hash256 commit_root,
    Hash256 spec_hash,
    RiskVector value,
    std::uint64_t projection,
    std::vector<RiskEvidence> evidence) {
    std::ranges::sort(evidence, [](const RiskEvidence& left, const RiskEvidence& right) {
        return evidence_key(left) < evidence_key(right);
    });

    return make_measurement_record(
        commit,
        commit_root,
        spec_hash,
        value,
        projection,
        evidence_hashes(evidence)).measurement_hash;
}

MeasuredRisk MeasuredRiskFunctional::measure_commit(
    const Repository& repository,
    std::string_view branch,
    CommitId commit,
    const MeasurementSpec& spec,
    const Verifier& verifier) const {
    return measure_commit(repository, branch, commit, spec, &verifier);
}

MeasuredRisk MeasuredRiskFunctional::measure_commit(
    const Repository& repository,
    std::string_view branch,
    CommitId commit,
    const MeasurementSpec& spec,
    const Verifier* verifier) const {
    MeasuredRisk risk;
    risk.commit = commit;
    const auto record = repository.backend().commit_record(commit);
    risk.commit_root = record.after_root;
    risk.spec_hash = measurement_spec_hash(spec);
    if (spec.structural) {
        add_component(risk, StructuralRiskMeasure{}.measure(repository, branch, commit));
    }
    if (spec.law) {
        add_component(risk, LawRiskMeasure{}.measure(record));
    }
    if (spec.repair) {
        add_component(risk, RepairDistanceMeasure{}.measure(repository, branch, commit, verifier, spec.repair_options));
    }
    if (spec.surprise) {
        add_component(risk, HistorySurpriseMeasure{}.measure(repository, branch, commit));
    }
    risk.projection = project(risk.value, spec.projection);
    risk.evidence_root = measurement_evidence_root(evidence_hashes(risk.evidence));
    risk.measurement_hash = measured_risk_hash(
        risk.commit,
        risk.commit_root,
        risk.spec_hash,
        risk.value,
        risk.projection,
        risk.evidence);
    risk.measurement_object = risk.measurement_hash;
    risk.projection_result = make_projection_result(risk.measurement_hash, risk.value, spec.projection);
    return risk;
}

MeasuredRisk MeasuredRiskFunctional::measure_commit(
    const Repository& repository,
    std::string_view branch,
    CommitId commit,
    const Verifier& verifier,
    RepairSearchOptions repair_options) const {
    return measure_commit(repository, branch, commit, spec_from_options(repair_options), &verifier);
}

MeasuredRisk MeasuredRiskFunctional::measure_commit(
    const Repository& repository,
    std::string_view branch,
    CommitId commit,
    const Verifier* verifier,
    RepairSearchOptions repair_options) const {
    return measure_commit(repository, branch, commit, spec_from_options(repair_options), verifier);
}

std::vector<MeasuredRisk> MeasuredRiskFunctional::measure_branch(
    const Repository& repository,
    std::string_view branch,
    const MeasurementSpec& spec,
    const Verifier* verifier) const {
    std::vector<MeasuredRisk> out;
    for (const auto& record : repository.backend().history(branch)) {
        if (record.origin == TransactionOrigin::Internal) {
            continue;
        }
        out.push_back(measure_commit(repository, branch, record.id, spec, verifier));
    }
    return out;
}

std::vector<MeasuredRisk> MeasuredRiskFunctional::measure_branch(
    const Repository& repository,
    std::string_view branch,
    const Verifier* verifier,
    RepairSearchOptions repair_options) const {
    return measure_branch(repository, branch, spec_from_options(repair_options), verifier);
}

RiskVector joined_risk(const std::vector<MeasuredRisk>& measured) noexcept {
    RiskVector out;
    for (const auto& item : measured) {
        out = join(out, item.value);
    }
    return out;
}

}  // namespace pv
