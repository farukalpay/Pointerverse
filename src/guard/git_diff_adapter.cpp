// SPDX-License-Identifier: Apache-2.0
#include "pv/guard/git_diff_adapter.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include <fmt/format.h>

namespace pv {
namespace {

struct DiffContext {
    std::filesystem::path repo_root;
    std::filesystem::path base_root;
    std::string repo_name;
    std::string base_name;
};

std::string shell_quote(const std::filesystem::path& path) {
    auto text = path.string();
    std::string out{"'"};
    for (const auto ch : text) {
        if (ch == '\'') {
            out += "'\\''";
        } else {
            out.push_back(ch);
        }
    }
    out.push_back('\'');
    return out;
}

std::string shell_quote(std::string_view value) {
    std::string out{"'"};
    for (const auto ch : value) {
        if (ch == '\'') {
            out += "'\\''";
        } else {
            out.push_back(ch);
        }
    }
    out.push_back('\'');
    return out;
}

std::string run_command(const std::string& command) {
    std::array<char, 4096> buffer{};
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
    if (pipe == nullptr) {
        throw std::runtime_error("cannot run git diff command");
    }
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

std::vector<std::string> split_tab(std::string_view line) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= line.size()) {
        const auto end = line.find('\t', start);
        if (end == std::string_view::npos) {
            out.emplace_back(line.substr(start));
            break;
        }
        out.emplace_back(line.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

bool starts_with(std::string_view value, std::string_view prefix) noexcept {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

std::string strip_prefix(std::string value, std::string_view prefix) {
    if (!prefix.empty() && starts_with(value, prefix)) {
        value.erase(0, prefix.size());
        if (!value.empty() && value.front() == '/') {
            value.erase(0, 1);
        }
    }
    return value;
}

std::string strip_absolute_prefix_variants(std::string value, const std::filesystem::path& prefix) {
    const auto text = prefix.generic_string();
    value = strip_prefix(std::move(value), text);
    if (!text.empty() && text.front() == '/') {
        value = strip_prefix(std::move(value), std::string_view{text}.substr(1));
    }
    return value;
}

std::string expand_git_brace_path(std::string path) {
    const auto open = path.find('{');
    const auto arrow = path.find(" => ", open == std::string::npos ? 0 : open);
    const auto close = path.find('}', arrow == std::string::npos ? 0 : arrow);
    if (open == std::string::npos || arrow == std::string::npos || close == std::string::npos) {
        return path;
    }

    const auto prefix = path.substr(0, open);
    const auto left = path.substr(open + 1, arrow - open - 1);
    const auto right = path.substr(arrow + 4, close - arrow - 4);
    const auto suffix = path.substr(close + 1);
    if (right == "dev/null") {
        return prefix + left + suffix;
    }
    return prefix + right + suffix;
}

std::string normalize_path(std::string path, const DiffContext& ctx = {}) {
    if (path.empty() || path == "/dev/null") {
        return {};
    }
    std::ranges::replace(path, '\\', '/');
    path = expand_git_brace_path(std::move(path));
    if (starts_with(path, "a/") || starts_with(path, "b/")) {
        path.erase(0, 2);
    }

    path = strip_absolute_prefix_variants(std::move(path), ctx.repo_root);
    path = strip_absolute_prefix_variants(std::move(path), ctx.base_root);
    path = strip_prefix(std::move(path), ctx.repo_name);
    path = strip_prefix(std::move(path), ctx.base_name);

    while (starts_with(path, "./")) {
        path.erase(0, 2);
    }
    return path;
}

GitDiffStatus parse_status(std::string_view value) noexcept {
    if (value.empty()) {
        return GitDiffStatus::Unknown;
    }
    switch (value.front()) {
    case 'M':
        return GitDiffStatus::Modified;
    case 'A':
        return GitDiffStatus::Added;
    case 'D':
        return GitDiffStatus::Deleted;
    case 'R':
        return GitDiffStatus::Renamed;
    case 'C':
        return GitDiffStatus::Copied;
    case 'T':
        return GitDiffStatus::TypeChanged;
    default:
        return GitDiffStatus::Unknown;
    }
}

int parse_similarity(std::string_view value) {
    if (value.size() < 2 || (value.front() != 'R' && value.front() != 'C')) {
        return 0;
    }
    int similarity = 0;
    for (std::size_t index = 1; index < value.size(); ++index) {
        if (!std::isdigit(static_cast<unsigned char>(value[index]))) {
            break;
        }
        similarity = similarity * 10 + (value[index] - '0');
    }
    return similarity;
}

std::string event_action(GitDiffStatus status) {
    switch (status) {
    case GitDiffStatus::Modified:
    case GitDiffStatus::TypeChanged:
        return "modify_file";
    case GitDiffStatus::Added:
    case GitDiffStatus::Copied:
        return "create_file";
    case GitDiffStatus::Deleted:
        return "delete_file";
    case GitDiffStatus::Renamed:
        return "rename_file";
    case GitDiffStatus::Unknown:
        return "touch_file";
    }
    return "touch_file";
}

std::string status_code(GitDiffStatus status) {
    switch (status) {
    case GitDiffStatus::Modified:
        return "M";
    case GitDiffStatus::Added:
        return "A";
    case GitDiffStatus::Deleted:
        return "D";
    case GitDiffStatus::Renamed:
        return "R";
    case GitDiffStatus::Copied:
        return "C";
    case GitDiffStatus::TypeChanged:
        return "T";
    case GitDiffStatus::Unknown:
        return "U";
    }
    return "U";
}

std::string key_for(const std::string& path) {
    return path;
}

void apply_numstat(std::vector<GitDiffEntry>& entries, std::string_view text, const DiffContext& ctx) {
    std::unordered_map<std::string, GitDiffEntry*> by_path;
    for (auto& entry : entries) {
        by_path.emplace(key_for(entry.path), &entry);
    }

    std::istringstream input{std::string{text}};
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        const auto fields = split_tab(line);
        if (fields.size() < 3) {
            continue;
        }
        const auto path = normalize_path(fields.back(), ctx);
        const auto iter = by_path.find(path);
        if (iter == by_path.end()) {
            continue;
        }
        if (fields[0] == "-" || fields[1] == "-") {
            iter->second->binary = true;
            continue;
        }
        iter->second->additions = std::stoi(fields[0]);
        iter->second->deletions = std::stoi(fields[1]);
    }
}

std::optional<int> parse_new_line_start(std::string_view hunk) {
    const auto plus = hunk.find('+');
    if (plus == std::string_view::npos) {
        return std::nullopt;
    }
    auto index = plus + 1;
    int value = 0;
    bool found = false;
    while (index < hunk.size() && std::isdigit(static_cast<unsigned char>(hunk[index]))) {
        found = true;
        value = value * 10 + (hunk[index] - '0');
        ++index;
    }
    if (!found) {
        return std::nullopt;
    }
    return value;
}

void apply_patch_lines(std::vector<GitDiffEntry>& entries, std::string_view text, const DiffContext& ctx) {
    std::unordered_map<std::string, GitDiffEntry*> by_path;
    for (auto& entry : entries) {
        by_path.emplace(key_for(entry.path), &entry);
    }

    GitDiffEntry* current = nullptr;
    int new_line = 0;
    std::istringstream input{std::string{text}};
    std::string line;
    while (std::getline(input, line)) {
        if (starts_with(line, "+++ ")) {
            auto path = line.substr(4);
            if (starts_with(path, "b/")) {
                path.erase(0, 2);
            }
            path = normalize_path(std::move(path), ctx);
            const auto iter = by_path.find(path);
            current = iter == by_path.end() ? nullptr : iter->second;
            continue;
        }
        if (starts_with(line, "@@ ")) {
            const auto parsed = parse_new_line_start(line);
            if (parsed.has_value()) {
                new_line = *parsed;
            }
            continue;
        }
        if (current == nullptr || line.empty()) {
            continue;
        }
        if (line.front() == '+' && !starts_with(line, "+++")) {
            current->added_lines.push_back(GitDiffLine{current->path, new_line, line.substr(1)});
            ++new_line;
        } else if (line.front() != '-' && !starts_with(line, "\\ No newline")) {
            ++new_line;
        }
    }
}

std::filesystem::path resolve_repo(std::filesystem::path repo) {
    if (repo.empty()) {
        repo = ".";
    }
    return std::filesystem::weakly_canonical(repo);
}

std::filesystem::path resolve_base_path(const std::filesystem::path& repo, std::string_view base) {
    const std::filesystem::path raw{std::string{base}};
    if (raw.is_absolute() && std::filesystem::is_directory(raw)) {
        return std::filesystem::weakly_canonical(raw);
    }
    const auto relative_to_repo = repo / raw;
    if (std::filesystem::is_directory(relative_to_repo)) {
        return std::filesystem::weakly_canonical(relative_to_repo);
    }
    if (std::filesystem::is_directory(raw)) {
        return std::filesystem::weakly_canonical(raw);
    }
    return {};
}

std::string normal_name_status_command(const std::filesystem::path& repo, std::string_view base, std::string_view head) {
    return fmt::format(
        "git -C {} diff --name-status --find-renames {}...{}",
        shell_quote(repo),
        shell_quote(base),
        shell_quote(head));
}

std::string normal_numstat_command(const std::filesystem::path& repo, std::string_view base, std::string_view head) {
    return fmt::format(
        "git -C {} diff --numstat --find-renames {}...{}",
        shell_quote(repo),
        shell_quote(base),
        shell_quote(head));
}

std::string normal_patch_command(const std::filesystem::path& repo, std::string_view base, std::string_view head) {
    return fmt::format(
        "git -C {} diff --no-ext-diff --unified=0 {}...{}",
        shell_quote(repo),
        shell_quote(base),
        shell_quote(head));
}

std::string no_index_command(std::string_view mode, const std::filesystem::path& base, const std::filesystem::path& repo) {
    return fmt::format(
        "git diff --no-index {} {} {} 2>/dev/null",
        mode,
        shell_quote(base),
        shell_quote(repo));
}

}  // namespace

std::string_view to_string(GitDiffStatus status) noexcept {
    switch (status) {
    case GitDiffStatus::Modified:
        return "modified";
    case GitDiffStatus::Added:
        return "added";
    case GitDiffStatus::Deleted:
        return "deleted";
    case GitDiffStatus::Renamed:
        return "renamed";
    case GitDiffStatus::Copied:
        return "copied";
    case GitDiffStatus::TypeChanged:
        return "type_changed";
    case GitDiffStatus::Unknown:
        return "unknown";
    }
    return "unknown";
}

std::vector<GitDiffEntry> parse_git_name_status(std::string_view text) {
    std::vector<GitDiffEntry> entries;
    std::istringstream input{std::string{text}};
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        const auto fields = split_tab(line);
        if (fields.size() < 2) {
            continue;
        }

        GitDiffEntry entry;
        entry.status = parse_status(fields[0]);
        entry.similarity = parse_similarity(fields[0]);
        if (entry.status == GitDiffStatus::Renamed || entry.status == GitDiffStatus::Copied) {
            if (fields.size() < 3) {
                continue;
            }
            entry.old_path = normalize_path(fields[1]);
            entry.path = normalize_path(fields[2]);
        } else {
            if (fields.size() >= 3) {
                entry.old_path = normalize_path(fields[1]);
                entry.path = normalize_path(fields.back());
            } else {
                entry.path = normalize_path(fields[1]);
            }
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::vector<EvidenceEvent> evidence_from_git_diff(const std::vector<GitDiffEntry>& entries) {
    std::vector<EvidenceEvent> events;
    events.reserve(entries.size());
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        EvidenceEvent event;
        event.source = "git-diff";
        event.event_id = fmt::format("{}-{}-{}", index + 1, status_code(entry.status), entry.path);
        event.actor = "Agent";
        event.action = event_action(entry.status);
        event.target = entry.path;
        event.target_type = "File";
        event.attributes.push_back({"path", entry.path});
        event.attributes.push_back({"status", std::string{to_string(entry.status)}});
        event.attributes.push_back({"additions", std::to_string(entry.additions)});
        event.attributes.push_back({"deletions", std::to_string(entry.deletions)});
        if (!entry.old_path.empty()) {
            event.attributes.push_back({"old_path", entry.old_path});
        }
        events.push_back(std::move(event));
    }
    return events;
}

std::vector<GitDiffEntry> GitDiffAdapter::read(const GitDiffOptions& options) const {
    const auto repo = resolve_repo(options.repo);
    const auto base_dir = resolve_base_path(repo, options.base);
    DiffContext ctx;
    ctx.repo_root = repo;
    ctx.repo_name = repo.filename().generic_string();

    std::string name_status;
    std::string numstat;
    std::string patch;
    if (!base_dir.empty()) {
        ctx.base_root = base_dir;
        ctx.base_name = base_dir.filename().generic_string();
        name_status = run_command(no_index_command("--name-status --find-renames", base_dir, repo));
        numstat = run_command(no_index_command("--numstat --find-renames", base_dir, repo));
        patch = run_command(no_index_command("--unified=0", base_dir, repo));
    } else {
        name_status = run_command(normal_name_status_command(repo, options.base, options.head));
        numstat = run_command(normal_numstat_command(repo, options.base, options.head));
        patch = run_command(normal_patch_command(repo, options.base, options.head));
    }

    auto entries = parse_git_name_status(name_status);
    for (auto& entry : entries) {
        entry.path = normalize_path(std::move(entry.path), ctx);
        entry.old_path = normalize_path(std::move(entry.old_path), ctx);
    }
    apply_numstat(entries, numstat, ctx);
    apply_patch_lines(entries, patch, ctx);
    return entries;
}

}  // namespace pv
