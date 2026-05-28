// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "pv/core/delta.hpp"
#include "pv/kernel/execution_plan.hpp"
#include "pv/kernel/program.hpp"

namespace pv {

struct VmResult {
    bool ok{false};
    Delta delta;
    ExecutionPlan plan;
    std::optional<CommitProof> proof;
    std::vector<VmDiagnostic> diagnostics;
};

class KernelVm {
public:
    [[nodiscard]] VmResult execute(const WorldSnapshot& before, const Program& program) const;
};

[[nodiscard]] std::string format_vm_diagnostics(std::span<const VmDiagnostic> diagnostics);

}  // namespace pv
