// SPDX-License-Identifier: Apache-2.0
#include "pv/sentinel/report.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <sstream>
#include <utility>

namespace pv {

bool SentinelReport::clean() const noexcept {
    for (const auto& issue : issues) {
        if (issue.error) {
            return false;
        }
    }
    return true;
}

void add_sentinel_error(SentinelReport& report, std::string worker, std::string message) {
    report.issues.push_back(SentinelIssue{std::move(worker), std::move(message), true});
}

void add_sentinel_warning(SentinelReport& report, std::string worker, std::string message) {
    report.issues.push_back(SentinelIssue{std::move(worker), std::move(message), false});
}

void merge_into(SentinelReport& target, const SentinelReport& source) {
    target.regions_checked += source.regions_checked;
    target.commits_checked += source.commits_checked;
    target.snapshots_checked += source.snapshots_checked;
    target.objects_checked += source.objects_checked;
    target.branch_refs_checked += source.branch_refs_checked;
    target.program_replays += source.program_replays;
    target.proof_mismatches += source.proof_mismatches;
    target.store_corruptions += source.store_corruptions;
    target.issues.insert(target.issues.end(), source.issues.begin(), source.issues.end());
    target.heartbeats.insert(target.heartbeats.end(), source.heartbeats.begin(), source.heartbeats.end());
    if (empty(target.measurement)) {
        target.measurement = source.measurement;
    }
}

std::string render_sentinel_report(const SentinelReport& report) {
    std::ostringstream output;
    output << "Pointerverse Sentinel\n";
    output << "---------------------\n";
    output << fmt::format("boot:              {}\n", report.clean() ? "clean" : "errors");
    output << fmt::format("regions checked:   {}\n", report.regions_checked);
    output << fmt::format("objects checked:   {}\n", report.objects_checked);
    output << fmt::format("commits checked:   {}\n", report.commits_checked);
    output << fmt::format("snapshots checked: {}\n", report.snapshots_checked);
    output << fmt::format("branch refs:       {}\n", report.branch_refs_checked);
    output << fmt::format("program replays:   {}\n", report.program_replays);
    output << fmt::format("proof mismatches:  {}\n", report.proof_mismatches);
    output << fmt::format("store corruptions: {}\n", report.store_corruptions);
    output << fmt::format(
        "worker heartbeats: {}\n",
        report.heartbeats.empty()
            ? "none"
            : (std::ranges::all_of(report.heartbeats, [](const Heartbeat& heartbeat) {
                   return heartbeat.healthy;
               }) ? "clean" : "errors"));
    output << fmt::format("measurement:       {}\n", to_hex(report.measurement));
    for (const auto& issue : report.issues) {
        output << fmt::format(
            "{} {}: {}\n",
            issue.error ? "error" : "warning",
            issue.worker,
            issue.message);
    }
    return output.str();
}

}  // namespace pv
