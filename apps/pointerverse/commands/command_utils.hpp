// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <functional>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pv/hash/canonical.hpp"
#include "pv/query/query.hpp"
#include "pv/runtime/replayer.hpp"
#include "pv/runtime/ids.hpp"
#include "pv/storage/integrity.hpp"
#include "pv/storage/repository.hpp"
#include "pv/trace/replayer.hpp"

namespace pv::app {

struct ExpectedHash {
    std::optional<Hash256> canonical;
    std::optional<std::uint64_t> legacy;
};

[[nodiscard]] int run_checked(const std::function<int()>& fn);
[[nodiscard]] std::string read_text_file(const std::string& path);
[[nodiscard]] ExpectedHash parse_expected_hash(const std::string& value);
[[nodiscard]] bool matches_expected_hash(Hash256 actual, const ExpectedHash& expected);
[[nodiscard]] std::string first_world_name(std::string_view jsonl, std::string fallback);
[[nodiscard]] std::string first_script_world_name(const std::string& path, std::string fallback);
[[nodiscard]] std::string short_hash(CommitId id);

void print_replay_report(const ReplayResult& result, std::string_view status);
void print_runtime_replay_report(const RuntimeReplayResult& result, std::string_view status);
void print_integrity_report(const IntegrityReport& report);
void print_query_result(const WorldSnapshot& snapshot, const QueryResult& result);

[[nodiscard]] QueryResult run_query(
    const Repository& repository,
    std::string_view branch,
    const std::vector<std::string>& terms);

[[nodiscard]] std::filesystem::path source_root();
[[nodiscard]] std::string shell_quote(const std::filesystem::path& path);

}  // namespace pv::app
