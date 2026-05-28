// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pv/core/id.hpp"
#include "pv/hash/canonical.hpp"
#include "pv/runtime/ids.hpp"

namespace pv {

struct BranchRef {
    std::string name;
    BranchId branch;
    CommitId head;
    Hash256 snapshot;
    Epoch epoch;
};

class RefStore {
public:
    explicit RefStore(std::filesystem::path root);

    void update_branch(const BranchRef& ref) const;
    [[nodiscard]] std::optional<BranchRef> read_branch(std::string_view name) const;
    [[nodiscard]] std::vector<BranchRef> list_branches() const;

    void set_current_branch(std::string_view name) const;
    [[nodiscard]] std::string current_branch() const;

    [[nodiscard]] std::filesystem::path branch_path(std::string_view name) const;
    [[nodiscard]] static bool valid_branch_name(std::string_view name);

private:
    std::filesystem::path root_;
};

}  // namespace pv
