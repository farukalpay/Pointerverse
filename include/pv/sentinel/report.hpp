// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "pv/hash/canonical.hpp"
#include "pv/sentinel/heartbeat.hpp"

namespace pv {

struct SentinelIssue {
    std::string worker;
    std::string message;
    bool error{true};
};

struct SentinelReport {
    std::size_t regions_checked{0};
    std::size_t commits_checked{0};
    std::size_t snapshots_checked{0};
    std::size_t objects_checked{0};
    std::size_t branch_refs_checked{0};
    std::size_t program_replays{0};
    std::size_t proof_mismatches{0};
    std::size_t store_corruptions{0};
    Hash256 measurement;
    std::vector<SentinelIssue> issues;
    std::vector<Heartbeat> heartbeats;

    [[nodiscard]] bool clean() const noexcept;
};

void add_sentinel_error(SentinelReport& report, std::string worker, std::string message);
void add_sentinel_warning(SentinelReport& report, std::string worker, std::string message);
void merge_into(SentinelReport& target, const SentinelReport& source);
[[nodiscard]] std::string render_sentinel_report(const SentinelReport& report);

}  // namespace pv
