// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace pv {

struct CanonicalEditCost {
    std::uint64_t value{0};
    std::uint64_t tokens{0};
    std::string canonical_script;
};

struct BaselineMdlProfile {
    std::map<std::string, std::uint64_t> token_counts;
    std::uint64_t total_tokens{0};

    friend bool operator==(const BaselineMdlProfile&, const BaselineMdlProfile&) = default;
};

[[nodiscard]] std::vector<std::string> canonical_repair_tokens(std::string_view script);
[[nodiscard]] std::string canonical_repair_script(std::string_view script);
[[nodiscard]] std::uint64_t canonical_edit_cost(std::string_view script);
[[nodiscard]] std::uint64_t baseline_mdl_edit_cost(
    std::string_view script,
    const BaselineMdlProfile& profile = {});

class IntrinsicEditCost {
public:
    [[nodiscard]] CanonicalEditCost measure(std::string_view script) const;
};

}  // namespace pv
