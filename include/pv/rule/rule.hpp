// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pv/law/law.hpp"
#include "pv/rule/pattern.hpp"

namespace pv {

struct Rule {
    std::string name;
    RelationPattern trigger;
    std::vector<RequirementPattern> requirements;
    Severity severity{Severity::Error};
    std::string reason;
};

class RuleBuilder {
public:
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] const std::string& name() const noexcept;

    [[nodiscard]] std::optional<Rule> consume_line(std::string_view line);
    void reset() noexcept;

private:
    Rule draft_;
    bool active_{false};
    bool has_trigger_{false};
};

[[nodiscard]] RelationPattern parse_relation_pattern(std::string_view text);

// Parse a requirement/forbid pattern. Identical to parse_relation_pattern except
// each endpoint may be prefixed with '~' to unpin it from the trigger: '~Type'
// matches any object of that type, bare '~' or '~*' matches any object, and a
// bare 'Type' (no prefix) stays pinned to the trigger's from/to (back-compat).
[[nodiscard]] RequirementPattern parse_requirement_pattern(std::string_view text);

[[nodiscard]] std::vector<Rule> parse_rules(std::string_view text);

}  // namespace pv
