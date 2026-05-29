// SPDX-License-Identifier: Apache-2.0
#include "pv/measure/risk_functional.hpp"

#include <algorithm>

#include "pv/hash/hasher.hpp"
#include "pv/kernel/canonical_codec.hpp"
#include "pv/measure/history_measure.hpp"
#include "pv/measure/law_measure.hpp"
#include "pv/measure/risk_lattice.hpp"
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

}  // namespace

Hash256 measured_risk_hash(CommitId commit, RiskVector value, std::vector<RiskEvidence> evidence) {
    std::ranges::sort(evidence, [](const RiskEvidence& left, const RiskEvidence& right) {
        return evidence_key(left) < evidence_key(right);
    });

    CanonicalWriter writer;
    writer.string("MeasuredRisk:v1");
    writer.hash(commit.value);
    writer.u64(value.structural);
    writer.u64(value.law_distance);
    writer.u64(value.repair_distance);
    writer.u64(value.surprise);
    writer.u64(evidence.size());
    for (const auto& item : evidence) {
        writer.hash(risk_evidence_hash(item));
    }
    return sha256(writer.bytes());
}

MeasuredRisk MeasuredRiskFunctional::measure_commit(
    const Repository& repository,
    std::string_view branch,
    CommitId commit,
    const Verifier& verifier,
    RepairSearchOptions repair_options) const {
    return measure_commit(repository, branch, commit, &verifier, repair_options);
}

MeasuredRisk MeasuredRiskFunctional::measure_commit(
    const Repository& repository,
    std::string_view branch,
    CommitId commit,
    const Verifier* verifier,
    RepairSearchOptions repair_options) const {
    MeasuredRisk risk;
    risk.commit = commit;
    const auto record = repository.backend().commit_record(commit);
    add_component(risk, StructuralRiskMeasure{}.measure(repository, branch, commit));
    add_component(risk, LawRiskMeasure{}.measure(record));
    add_component(risk, RepairDistanceMeasure{}.measure(repository, branch, commit, verifier, repair_options));
    add_component(risk, HistorySurpriseMeasure{}.measure(repository, branch, commit));
    risk.measurement_hash = measured_risk_hash(risk.commit, risk.value, risk.evidence);
    return risk;
}

std::vector<MeasuredRisk> MeasuredRiskFunctional::measure_branch(
    const Repository& repository,
    std::string_view branch,
    const Verifier* verifier,
    RepairSearchOptions repair_options) const {
    std::vector<MeasuredRisk> out;
    for (const auto& record : repository.backend().history(branch)) {
        if (record.origin == TransactionOrigin::Internal) {
            continue;
        }
        out.push_back(measure_commit(repository, branch, record.id, verifier, repair_options));
    }
    return out;
}

RiskVector joined_risk(const std::vector<MeasuredRisk>& measured) noexcept {
    RiskVector out;
    for (const auto& item : measured) {
        out = join(out, item.value);
    }
    return out;
}

}  // namespace pv

