// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <string>

#include "pv/guard/report.hpp"
#include "pv/ingest/ingestion_pipeline.hpp"

namespace pv {

struct GuardRunOptions {
    std::filesystem::path repo{"."};
    std::string base{"origin/main"};
    std::string head{"HEAD"};
    std::string mode{"observe"};
    std::string format{"text"};
    std::filesystem::path out;
    std::filesystem::path markdown_out;
    std::filesystem::path json_out;
    std::filesystem::path sarif_out;
    std::filesystem::path store;
    std::string branch{"guard"};
    std::string baseline;
    GuardStrictPolicy strict_policy;
    bool write_default_artifacts{true};
};

struct GuardRunResult {
    GuardReport report;
    IngestionResult ingestion;
    bool strict_failed{false};
};

[[nodiscard]] GuardRunResult run_guard(const GuardRunOptions& options);
[[nodiscard]] bool guard_strict_failed(const GuardReport& report) noexcept;

}  // namespace pv
