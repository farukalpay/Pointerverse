// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "pv/guard/finding.hpp"
#include "pv/guard/git_diff_adapter.hpp"

namespace pv {

class PrGuardPolicyPack {
public:
    [[nodiscard]] std::vector<GuardFinding> evaluate(const std::vector<GitDiffEntry>& entries) const;
};

}  // namespace pv
