// SPDX-License-Identifier: Apache-2.0
#include "pv/measure/risk_functional.hpp"

#include <algorithm>
#include <utility>

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
    auto namespace_id = component.namespace_id;
    auto functional_id = component.functional_id;
    if (namespace_id.empty()) {
        namespace_id = component.name;
    }
    if (functional_id.empty()) {
        if (component.name == "structural") {
            functional_id = "compat_projection";
        } else if (component.name == "law") {
            functional_id = "total_magnitude";
        } else if (component.name == "repair") {
            functional_id = "distance";
        } else if (component.name == "surprise") {
            functional_id = "history_distance";
        }
    }
    risk.lattice.coordinates.push_back(RiskCoordinate{namespace_id, functional_id, component.value});
    risk.components.push_back(component);
    risk.evidence.push_back(component.evidence);
}

std::vector<Hash256> evidence_hashes(const std::vector<RiskEvidence>& evidence) {
    std::vector<Hash256> out;
    out.reserve(evidence.size());
    for (const auto& item : evidence) {
        out.push_back(risk_evidence_hash(item));
    }
    return out;
}

std::vector<MeasurementComponentRecord> component_records(const std::vector<MeasuredComponent>& components) {
    std::vector<MeasurementComponentRecord> out;
    out.reserve(components.size());
    for (const auto& component : components) {
        out.push_back(make_measurement_component_record(
            component.namespace_id,
            component.functional_id,
            component.evidence.input_root,
            component.evidence.output_root,
            component.value,
            risk_evidence_hash(component.evidence)));
    }
    std::ranges::sort(out, [](const auto& left, const auto& right) {
        return to_hex(left.component_hash) < to_hex(right.component_hash);
    });
    return out;
}

std::vector<Hash256> component_hashes(const std::vector<MeasurementComponentRecord>& components) {
    std::vector<Hash256> out;
    out.reserve(components.size());
    for (const auto& component : components) {
        out.push_back(component.component_hash);
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
    std::vector<MeasuredComponent> components) {
    std::vector<RiskEvidence> evidence;
    evidence.reserve(components.size());
    for (const auto& component : components) {
        evidence.push_back(component.evidence);
    }
    const auto records = component_records(components);
    return make_measurement_record(
        commit,
        commit_root,
        spec_hash,
        component_hashes(records),
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
        for (const auto& component : StructuralRiskMeasure{}.measure_components(repository, branch, commit)) {
            add_component(risk, component);
        }
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
    risk.lattice = canonical_risk_lattice(std::move(risk.lattice));
    risk.value = risk_vector_from_lattice(risk.lattice);
    risk.component_records = component_records(risk.components);
    risk.component_root = measurement_component_root(component_hashes(risk.component_records));
    risk.projection = project(risk.lattice, spec.projection);
    risk.evidence_root = measurement_evidence_root(evidence_hashes(risk.evidence));
    const auto measurement_record = make_measurement_record(
        risk.commit,
        risk.commit_root,
        risk.spec_hash,
        component_hashes(risk.component_records),
        evidence_hashes(risk.evidence));
    risk.measurement_identity_hash = measurement_record.measurement_identity_hash;
    risk.measurement_object_hash = measurement_record.measurement_object_hash;
    risk.measurement_hash = measurement_record.measurement_identity_hash;
    risk.measurement_object = measurement_record.measurement_object_hash;
    risk.projection_result = make_projection_result(risk.measurement_hash, risk.lattice, spec.projection);
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

RiskVector joined_risk(const std::vector<MeasuredRisk>& measured) {
    return risk_vector_from_lattice(joined_lattice(measured));
}

RiskLatticeElement joined_lattice(const std::vector<MeasuredRisk>& measured) {
    RiskLatticeElement out;
    for (const auto& item : measured) {
        out = join(std::move(out), item.lattice);
    }
    return out;
}

}  // namespace pv
