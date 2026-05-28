// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "pv/ingest/evidence.hpp"

namespace pv {

enum class GitDiffStatus {
    Modified,
    Added,
    Deleted,
    Renamed,
    Copied,
    TypeChanged,
    Unknown
};

struct GitDiffLine {
    std::string file;
    int line{0};
    std::string text;
};

struct GitDiffEntry {
    GitDiffStatus status{GitDiffStatus::Unknown};
    std::string path;
    std::string old_path;
    int similarity{0};
    int additions{0};
    int deletions{0};
    bool binary{false};
    std::vector<GitDiffLine> added_lines;
};

struct GitDiffOptions {
    std::filesystem::path repo;
    std::string base;
    std::string head{"HEAD"};
};

[[nodiscard]] std::string_view to_string(GitDiffStatus status) noexcept;
[[nodiscard]] std::vector<GitDiffEntry> parse_git_name_status(std::string_view text);
[[nodiscard]] std::vector<EvidenceEvent> evidence_from_git_diff(const std::vector<GitDiffEntry>& entries);

class GitDiffAdapter {
public:
    [[nodiscard]] std::vector<GitDiffEntry> read(const GitDiffOptions& options) const;
};

}  // namespace pv
