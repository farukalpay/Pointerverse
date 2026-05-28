// SPDX-License-Identifier: Apache-2.0
#include "pv/storage/ref_store.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <fmt/format.h>

namespace pv {
namespace {

std::string read_first_line(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::string line;
    std::getline(input, line);
    return line;
}

void ensure_valid_name(std::string_view name) {
    if (!RefStore::valid_branch_name(name)) {
        throw std::invalid_argument(fmt::format("invalid branch name '{}'", name));
    }
}

}  // namespace

RefStore::RefStore(std::filesystem::path root) : root_(std::move(root)) {
    std::filesystem::create_directories(root_ / "refs" / "branches");
}

bool RefStore::valid_branch_name(std::string_view name) {
    if (name.empty() || name.front() == '/') {
        return false;
    }

    std::size_t component_start = 0;
    while (component_start < name.size()) {
        const auto slash = name.find('/', component_start);
        const auto component_end = slash == std::string_view::npos ? name.size() : slash;
        const auto component = name.substr(component_start, component_end - component_start);
        if (component.empty() || component == "." || component == "..") {
            return false;
        }
        component_start = component_end + 1;
        if (slash == std::string_view::npos) {
            break;
        }
    }
    return true;
}

std::filesystem::path RefStore::branch_path(std::string_view name) const {
    ensure_valid_name(name);
    return root_ / "refs" / "branches" / std::filesystem::path{std::string{name}};
}

void RefStore::update_branch(const BranchRef& ref) const {
    ensure_valid_name(ref.name);
    const auto path = branch_path(ref.name);
    std::filesystem::create_directories(path.parent_path());
    const auto tmp = path.string() + ".tmp";
    std::ofstream output(tmp, std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot write branch ref");
    }
    output << "branch " << ref.branch.value << '\n';
    output << "commit " << to_hex(ref.head.value) << '\n';
    output << "snapshot " << to_hex(ref.snapshot) << '\n';
    output << "epoch " << ref.epoch.value << '\n';
    output.close();
    std::filesystem::rename(tmp, path);
}

std::optional<BranchRef> RefStore::read_branch(std::string_view name) const {
    ensure_valid_name(name);
    const auto path = branch_path(name);
    if (!std::filesystem::exists(path)) {
        return std::nullopt;
    }

    BranchRef ref;
    ref.name = std::string{name};
    std::ifstream input(path);
    std::string key;
    while (input >> key) {
        if (key == "branch") {
            input >> ref.branch.value;
            continue;
        }
        if (key == "commit") {
            std::string value;
            input >> value;
            const auto parsed = parse_hash256(value);
            if (!parsed.has_value()) {
                throw std::runtime_error("invalid commit hash in branch ref");
            }
            ref.head = CommitId{*parsed};
            continue;
        }
        if (key == "snapshot") {
            std::string value;
            input >> value;
            const auto parsed = parse_hash256(value);
            if (!parsed.has_value()) {
                throw std::runtime_error("invalid snapshot hash in branch ref");
            }
            ref.snapshot = *parsed;
            continue;
        }
        if (key == "epoch") {
            input >> ref.epoch.value;
            continue;
        }
        std::string ignored;
        std::getline(input, ignored);
    }
    if (!ref.branch.valid() || !ref.head.valid() || empty(ref.snapshot)) {
        throw std::runtime_error("incomplete branch ref '" + std::string{name} + "'");
    }
    return ref;
}

std::vector<BranchRef> RefStore::list_branches() const {
    std::vector<BranchRef> refs;
    const auto root = root_ / "refs" / "branches";
    if (!std::filesystem::exists(root)) {
        return refs;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() == ".tmp") {
            continue;
        }
        const auto name = entry.path().lexically_relative(root).generic_string();
        if (auto ref = read_branch(name); ref.has_value()) {
            refs.push_back(std::move(*ref));
        }
    }
    std::ranges::sort(refs, [](const BranchRef& left, const BranchRef& right) {
        return left.name < right.name;
    });
    return refs;
}

void RefStore::set_current_branch(std::string_view name) const {
    ensure_valid_name(name);
    std::filesystem::create_directories(root_);
    const auto path = root_ / "HEAD";
    const auto tmp = root_ / "HEAD.tmp";
    std::ofstream output(tmp, std::ios::trunc);
    output << name << '\n';
    output.close();
    std::filesystem::rename(tmp, path);
}

std::string RefStore::current_branch() const {
    const auto path = root_ / "HEAD";
    if (!std::filesystem::exists(path)) {
        return "main";
    }
    auto line = read_first_line(path);
    return line.empty() ? "main" : line;
}

}  // namespace pv
