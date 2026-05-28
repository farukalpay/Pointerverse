// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "pv/law/law.hpp"
#include "pv/rule/rule.hpp"

namespace pv {

class PatternLaw final : public Law {
public:
    explicit PatternLaw(Rule rule);

    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] std::vector<LawViolation> check(const LawCheckContext& ctx) const override;
    [[nodiscard]] const Rule& rule() const noexcept;

private:
    Rule rule_;
};

class RuleEngine {
public:
    void add(Rule rule);
    void add_all(std::vector<Rule> rules);

    [[nodiscard]] bool contains(std::string_view name) const noexcept;
    [[nodiscard]] std::optional<Rule> find(std::string_view name) const;
    [[nodiscard]] std::shared_ptr<Law> make_law(std::string_view name) const;
    [[nodiscard]] const std::vector<Rule>& rules() const noexcept;

private:
    std::vector<Rule> rules_;
};

}  // namespace pv
