// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "pv/guard/finding.hpp"
#include "pv/measure/risk_functional.hpp"
#include "pv/runtime/ids.hpp"

namespace pv {

struct CalibrationBaseline;

struct GuardStrictPolicy {
    bool fail_on_law_distance{true};
    bool fail_on_repair_distance{true};
    double structural_percentile{0.95};
    double surprise_percentile{0.95};
    std::size_t min_history_commits_for_calibration{30};
};

struct GuardStrictDecision {
    bool failed{false};
    bool law_distance_failed{false};
    bool repair_distance_failed{false};
    bool structural_failed{false};
    bool surprise_failed{false};
    bool baseline_contaminated{false};
    std::uint64_t structural_threshold{0};
    std::uint64_t surprise_threshold{0};
    std::size_t calibration_commits{0};
    std::vector<std::string> warnings;
};

struct GuardReport {
    std::string repo;
    std::string base;
    std::string head;
    std::string mode;
    Hash256 measurement_spec_hash;
    std::string baseline;
    Hash256 baseline_hash;
    RiskVector measured_risk;
    std::uint64_t projected_score{0};
    std::vector<MeasuredRisk> measured_risks;
    GuardStrictPolicy strict_policy;
    GuardStrictDecision strict_decision;
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
[[nodiscard]] GuardStrictDecision strict_decision_for(
    const GuardReport& report,
    const CalibrationBaseline* baseline = nullptr);
[[nodiscard]] std::string render_guard_report_text(const GuardReport& report);
[[nodiscard]] std::string render_guard_report_markdown(const GuardReport& report);
[[nodiscard]] std::string render_guard_report_json(const GuardReport& report);
[[nodiscard]] std::string render_guard_report_sarif(const GuardReport& report);
[[nodiscard]] std::string render_guard_report(const GuardReport& report, std::string_view format);

}  // namespace pv
