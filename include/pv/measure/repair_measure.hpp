// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "pv/core/delta.hpp"
#include "pv/hash/canonical.hpp"
#include "pv/law/verifier.hpp"
#include "pv/measure/risk_evidence.hpp"
#include "pv/runtime/ids.hpp"

namespace pv {

class Repository;

struct RepairSearchOptions {
    std::uint32_t max_depth{3};
    std::uint32_t max_candidates{256};
};

struct RepairOperator {
    std::string name;
    Delta delta;
    ObjectId object;
    PointerId pointer;
    std::string attribute;
};

struct RepairBasis {
    std::vector<RepairOperator> operators;
    Hash256 basis_hash;
};

enum class RepairSolveStatus {
    Accepted,
    VerifierUnavailable,
    Found,
    Exhausted
};

struct RepairWitness {
    Delta delta;
    std::vector<Hash256> operation_hashes;
    Hash256 operation_batch_hash;
};

struct RepairSolveResult {
    RepairBasis basis;
    RepairWitness witness;
    RepairSolveStatus status{RepairSolveStatus::Accepted};
    std::uint32_t depth{0};
    std::size_t frontier_size{0};
};

[[nodiscard]] Hash256 repair_operator_hash(const RepairOperator& op);
[[nodiscard]] Hash256 repair_basis_hash(RepairBasis basis);
[[nodiscard]] Hash256 repair_operation_batch_hash(std::vector<Hash256> operations);

class RepairSolver {
public:
    [[nodiscard]] RepairSolveResult solve(
        const Repository& repository,
        std::string_view branch,
        CommitId commit,
        const Verifier* verifier,
        RepairSearchOptions options = {}) const;

    [[nodiscard]] RepairSolveResult solve(
        const Repository& repository,
        std::string_view branch,
        CommitId commit,
        const Verifier& verifier,
        RepairSearchOptions options = {}) const;
};

class RepairDistanceMeasure {
public:
    [[nodiscard]] MeasuredComponent measure(
        const Repository& repository,
        std::string_view branch,
        CommitId commit,
        const Verifier* verifier,
        RepairSearchOptions options = {}) const;

    [[nodiscard]] MeasuredComponent measure(
        const Repository& repository,
        std::string_view branch,
        CommitId commit,
        const Verifier& verifier,
        RepairSearchOptions options = {}) const;
};

}  // namespace pv
