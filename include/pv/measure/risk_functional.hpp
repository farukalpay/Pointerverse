// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "pv/law/verifier.hpp"
#include "pv/measure/measurement_spec.hpp"
#include "pv/measure/repair_measure.hpp"
#include "pv/measure/risk_evidence.hpp"
#include "pv/measure/risk_projection.hpp"
#include "pv/measure/risk_value.hpp"
#include "pv/runtime/ids.hpp"

namespace pv {

class Repository;

struct MeasuredRisk {
    CommitId commit;
    Hash256 commit_root;
    Hash256 spec_hash;
    RiskVector value;
    std::uint64_t projection{0};
    ProjectionResult projection_result;
    std::vector<RiskEvidence> evidence;
    Hash256 evidence_root;
    Hash256 measurement_object;
    Hash256 measurement_hash;
};

class MeasuredRiskFunctional {
public:
    [[nodiscard]] MeasuredRisk measure_commit(
        const Repository& repository,
        std::string_view branch,
        CommitId commit,
        const MeasurementSpec& spec,
        const Verifier* verifier = nullptr) const;

    [[nodiscard]] MeasuredRisk measure_commit(
        const Repository& repository,
        std::string_view branch,
        CommitId commit,
        const MeasurementSpec& spec,
        const Verifier& verifier) const;

    [[nodiscard]] std::vector<MeasuredRisk> measure_branch(
        const Repository& repository,
        std::string_view branch,
        const MeasurementSpec& spec,
        const Verifier* verifier = nullptr) const;

    [[nodiscard]] MeasuredRisk measure_commit(
        const Repository& repository,
        std::string_view branch,
        CommitId commit,
        const Verifier* verifier = nullptr,
        RepairSearchOptions repair_options = {}) const;

    [[nodiscard]] MeasuredRisk measure_commit(
        const Repository& repository,
        std::string_view branch,
        CommitId commit,
        const Verifier& verifier,
        RepairSearchOptions repair_options = {}) const;

    [[nodiscard]] std::vector<MeasuredRisk> measure_branch(
        const Repository& repository,
        std::string_view branch,
        const Verifier* verifier = nullptr,
        RepairSearchOptions repair_options = {}) const;
};

[[nodiscard]] RiskVector joined_risk(const std::vector<MeasuredRisk>& measured) noexcept;
[[nodiscard]] Hash256 measured_risk_hash(
    CommitId commit,
    Hash256 commit_root,
    Hash256 spec_hash,
    RiskVector value,
    std::uint64_t projection,
    std::vector<RiskEvidence> evidence);

}  // namespace pv
