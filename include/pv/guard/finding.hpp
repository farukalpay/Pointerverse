// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pv/runtime/ids.hpp"

namespace pv {

enum class FindingSeverity {
    Info,
    Low,
    Medium,
    High,
    Critical
};

struct GuardFinding {
    FindingSeverity severity{FindingSeverity::Info};
    std::string rule;
    std::string message;
    std::string file;
    std::optional<int> line;
    std::vector<CommitId> evidence_commits;
};

[[nodiscard]] std::string_view to_string(FindingSeverity severity) noexcept;
[[nodiscard]] int risk_points(FindingSeverity severity) noexcept;

}  // namespace pv
