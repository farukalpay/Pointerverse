// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "pv/guard/finding.hpp"
#include "pv/runtime/ids.hpp"

namespace pv {

struct GuardReport {
    std::string repo;
    std::string base;
    std::string head;
    std::string mode;
    int risk_score{0};
    std::string status{"clean"};
    std::size_t changed_files{0};
    int additions{0};
    int deletions{0};
    std::size_t ingested_events{0};
    std::size_t ingestion_violations{0};
    std::vector<std::string> affected_files;
    std::vector<GuardFinding> findings;
    std::vector<CommitId> evidence_commits;
    std::vector<std::string> artifacts;
};

[[nodiscard]] int guard_risk_score(const std::vector<GuardFinding>& findings) noexcept;
[[nodiscard]] std::string guard_status_for_risk(int risk_score);
[[nodiscard]] std::string render_guard_report_text(const GuardReport& report);
[[nodiscard]] std::string render_guard_report_markdown(const GuardReport& report);
[[nodiscard]] std::string render_guard_report_json(const GuardReport& report);
[[nodiscard]] std::string render_guard_report_sarif(const GuardReport& report);
[[nodiscard]] std::string render_guard_report(const GuardReport& report, std::string_view format);

}  // namespace pv
