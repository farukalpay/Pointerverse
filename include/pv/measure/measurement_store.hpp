// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include "pv/law/verifier.hpp"
#include "pv/measure/measurement_index.hpp"
#include "pv/measure/measurement_record.hpp"
#include "pv/measure/measurement_spec.hpp"
#include "pv/measure/risk_functional.hpp"

namespace pv {

class Repository;

struct MeasurementLoadResult {
    MeasuredRisk measured;
    MeasurementRecord record;
    bool cache_hit{false};
};

struct MeasurementBranchResult {
    std::vector<MeasuredRisk> measured;
    std::vector<MeasurementRecord> records;
    std::size_t cache_hits{0};
    std::size_t cache_misses{0};
};

class MeasurementStore {
public:
    explicit MeasurementStore(Repository& repository);

    [[nodiscard]] Hash256 put_spec(const MeasurementSpec& spec) const;
    [[nodiscard]] MeasurementSpec load_spec(Hash256 spec_hash) const;
    [[nodiscard]] MeasurementRecord load_record(Hash256 measurement_object) const;

    [[nodiscard]] MeasurementLoadResult measure_or_load_commit(
        std::string_view branch,
        CommitId commit,
        const MeasurementSpec& spec,
        const Verifier* verifier = nullptr) const;
    [[nodiscard]] MeasurementBranchResult measure_or_load_branch(
        std::string_view branch,
        const MeasurementSpec& spec,
        const Verifier* verifier = nullptr) const;
    [[nodiscard]] MeasurementBranchResult measure_new_commits(
        std::string_view branch,
        const std::vector<CommitId>& existing_history,
        const MeasurementSpec& spec,
        const Verifier* verifier = nullptr) const;
    [[nodiscard]] MeasurementBranchResult rebuild_cache(
        std::string_view branch,
        const MeasurementSpec& spec,
        const Verifier* verifier = nullptr) const;

    [[nodiscard]] const MeasurementIndex& index() const noexcept;

private:
    [[nodiscard]] MeasurementLoadResult compute_and_store(
        std::string_view branch,
        CommitId commit,
        const MeasurementSpec& spec,
        const Verifier* verifier) const;
    [[nodiscard]] std::optional<MeasurementLoadResult> load_cached(
        std::string_view branch,
        CommitId commit,
        Hash256 spec_hash) const;

    Repository& repository_;
    MeasurementIndex index_;
};

[[nodiscard]] RiskVector joined_risk(const MeasurementBranchResult& result) noexcept;

}  // namespace pv
