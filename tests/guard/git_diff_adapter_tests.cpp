// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

#include "pv/guard/git_diff_adapter.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_path(std::string_view name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_guard_diff_" + std::string{name} + "_" + std::to_string(stamp));
    std::filesystem::remove_all(path);
    return path;
}

void write_file(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    output << text;
}

}  // namespace

TEST_CASE("git name-status parser maps statuses and renames") {
    const auto entries = parse_git_name_status(
        "M\tsrc/main.cpp\n"
        "A\ttests/test_main.cpp\n"
        "D\told.cpp\n"
        "R100\told_name.cpp\tnew_name.cpp\n");

    REQUIRE(entries.size() == 4);
    REQUIRE(entries[0].status == GitDiffStatus::Modified);
    REQUIRE(entries[0].path == "src/main.cpp");
    REQUIRE(entries[1].status == GitDiffStatus::Added);
    REQUIRE(entries[2].status == GitDiffStatus::Deleted);
    REQUIRE(entries[3].status == GitDiffStatus::Renamed);
    REQUIRE(entries[3].old_path == "old_name.cpp");
    REQUIRE(entries[3].path == "new_name.cpp");
    REQUIRE(entries[3].similarity == 100);
}

TEST_CASE("git diff entries map to evidence events") {
    const auto entries = parse_git_name_status(
        "M\tsrc/main.cpp\n"
        "A\ttests/test_main.cpp\n"
        "D\told.cpp\n"
        "R100\told.cpp\tnew.cpp\n");
    const auto events = evidence_from_git_diff(entries);

    REQUIRE(events.size() == 4);
    REQUIRE(events[0].source == "git-diff");
    REQUIRE(events[0].actor == "Agent");
    REQUIRE(events[0].action == "modify_file");
    REQUIRE(events[0].target == "src/main.cpp");
    REQUIRE(events[1].action == "create_file");
    REQUIRE(events[2].action == "delete_file");
    REQUIRE(events[3].action == "rename_file");
}

TEST_CASE("git diff adapter supports no-index directory demo paths") {
    const auto root = temp_path("directory");
    const auto before = root / "before";
    const auto after = root / "after";
    write_file(before / "src" / "auth.cpp", "int login() { return 0; }\n");
    write_file(after / "src" / "auth.cpp", "int login() { return 1; }\n");
    write_file(after / "config" / "dev.env", "SECRET_KEY=demo\n");

    const auto entries = GitDiffAdapter{}.read(GitDiffOptions{after, "../before", "HEAD"});

    REQUIRE(entries.size() == 2);
    REQUIRE(entries[0].path.find("after/") == std::string::npos);
    REQUIRE(entries[1].path.find("after/") == std::string::npos);
    REQUIRE_FALSE(entries[0].path.empty());
    REQUIRE_FALSE(entries[1].path.empty());

    std::filesystem::remove_all(root);
}
