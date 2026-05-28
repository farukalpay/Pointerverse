// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::filesystem::path source_root() {
    return std::filesystem::path{POINTERVERSE_SOURCE_ROOT};
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    return std::string{std::istreambuf_iterator<char>{input}, {}};
}

bool is_source_file(const std::filesystem::path& path) {
    const auto ext = path.extension().string();
    return ext == ".hpp" || ext == ".cpp" || ext == ".md" || ext == ".json"
        || ext == ".toml" || ext == ".pv" || ext == ".pvdomain" || ext == ".sh"
        || ext == ".yml" || ext == ".yaml";
}

bool path_contains(const std::filesystem::path& path, std::string_view segment) {
    return std::ranges::any_of(path, [&](const auto& part) {
        return part == segment;
    });
}

std::vector<std::filesystem::path> files_under(const std::vector<std::filesystem::path>& roots) {
    std::vector<std::filesystem::path> files;
    for (const auto& root : roots) {
        if (!std::filesystem::exists(root)) {
            continue;
        }
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
            if (entry.is_regular_file() && is_source_file(entry.path())) {
                files.push_back(entry.path());
            }
        }
    }
    return files;
}

bool includes_any_forbidden(const std::filesystem::path& file, const std::vector<std::string>& forbidden) {
    const auto text = read_file(file);
    return std::ranges::any_of(forbidden, [&](const std::string& include) {
        return text.find("#include \"" + include) != std::string::npos
            || text.find("#include <" + include) != std::string::npos;
    });
}

bool has_forbidden_surface_word(const std::string& text) {
    for (std::size_t index = 0; index + 3 <= text.size(); ++index) {
        const auto word = text.substr(index, 3);
        auto lower = word;
        std::ranges::transform(lower, lower.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        const std::array<char, 3> forbidden_chars{'l', 'a', 'b'};
        const std::string forbidden{forbidden_chars.begin(), forbidden_chars.end()};
        if (lower != forbidden) {
            continue;
        }
        const auto before = index == 0 ? '\0' : text[index - 1];
        const auto after = index + 3 >= text.size() ? '\0' : text[index + 3];
        const auto word_char = [](char ch) {
            return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
        };
        if (!word_char(before) && !word_char(after)) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("layered targets do not include higher product surfaces") {
    const auto root = source_root();
    const std::vector<std::string> app_forbidden{
        "pv/storage/", "pv/sentinel/", "pv/guard/", "pv/audit/", "pv/ingest/", "apps/"
    };
    for (const auto& file : files_under({
             root / "include" / "pv" / "kernel",
             root / "src" / "kernel",
             root / "include" / "pv" / "core",
             root / "src" / "core",
             root / "include" / "pv" / "hash",
             root / "src" / "hash",
             root / "include" / "pv" / "law",
             root / "src" / "law",
             root / "include" / "pv" / "trace",
             root / "src" / "trace"})) {
        if (file.filename() == "replayer.cpp" || file.filename() == "world.cpp") {
            continue;
        }
        CAPTURE(file.string());
        REQUIRE_FALSE(includes_any_forbidden(file, app_forbidden));
    }

    const std::vector<std::string> runtime_forbidden{"pv/storage/", "pv/sentinel/", "pv/guard/", "pv/audit/", "pv/ingest/", "apps/"};
    for (const auto& file : files_under({root / "include" / "pv" / "runtime", root / "src" / "runtime"})) {
        CAPTURE(file.string());
        REQUIRE_FALSE(includes_any_forbidden(file, runtime_forbidden));
    }

    const std::vector<std::string> storage_forbidden{"pv/sentinel/", "pv/guard/", "pv/audit/", "pv/ingest/", "apps/"};
    for (const auto& file : files_under({root / "include" / "pv" / "storage", root / "src" / "storage"})) {
        CAPTURE(file.string());
        REQUIRE_FALSE(includes_any_forbidden(file, storage_forbidden));
    }
}

TEST_CASE("pointerverse main stays a registry entry point") {
    const auto main = read_file(source_root() / "apps" / "pointerverse" / "main.cpp");
    REQUIRE(main.find("pv/audit/") == std::string::npos);
    REQUIRE(main.find("pv/guard/") == std::string::npos);
    REQUIRE(main.find("pv/sentinel/") == std::string::npos);
    REQUIRE(main.find("pv/storage/") == std::string::npos);
}

TEST_CASE("old local surface name is absent from tracked source files") {
    const auto root = source_root();
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file() || !is_source_file(entry.path())) {
            continue;
        }
        const auto rel = entry.path().lexically_relative(root);
        if (path_contains(rel, ".git") || path_contains(rel, "build")) {
            continue;
        }
        CAPTURE(rel.string());
        REQUIRE_FALSE(has_forbidden_surface_word(read_file(entry.path())));
    }
}
