// SPDX-License-Identifier: Apache-2.0
#include "pv/rule/rule_engine.hpp"

#include <fmt/format.h>

#include <stdexcept>
#include <utility>

namespace pv {
namespace {

void replace_all(std::string& value, std::string_view from, std::string_view to) {
    std::size_t offset = 0;
    while ((offset = value.find(from, offset)) != std::string::npos) {
        value.replace(offset, from.size(), to);
        offset += to.size();
    }
}

std::string expand_reason(std::string reason, const PatternMatch& match) {
    replace_all(reason, "{from}", match.from_name);
    replace_all(reason, "{to}", match.to_name);
    replace_all(reason, "{relation}", match.relation);
    return reason;
}

}  // namespace

PatternLaw::PatternLaw(Rule rule) : rule_(std::move(rule)) {
    if (rule_.name.empty()) {
        throw std::invalid_argument("rule law name cannot be empty");
    }
}

std::string_view PatternLaw::name() const {
    return rule_.name;
}

std::vector<LawViolation> PatternLaw::check(const LawCheckContext& ctx) const {
    std::vector<LawViolation> violations;
    for (const auto& match : trigger_matches(ctx, rule_.trigger)) {
        bool satisfied = true;
        for (const auto& requirement : rule_.requirements) {
            if (!requirement_exists(ctx, match, requirement)) {
                satisfied = false;
                break;
            }
        }
        if (satisfied && !rule_.requirements.empty()) {
            continue;
        }

        violations.push_back(LawViolation{
            rule_.name,
            rule_.severity,
            1.0,
            rule_.reason.empty()
                ? fmt::format("{} {} {} violates {}", match.from_name, match.relation, match.to_name, rule_.name)
                : expand_reason(rule_.reason, match)
        });
    }
    return violations;
}

const Rule& PatternLaw::rule() const noexcept {
    return rule_;
}

void RuleEngine::add(Rule rule) {
    if (rule.name.empty()) {
        throw std::invalid_argument("rule name cannot be empty");
    }
    for (auto& existing : rules_) {
        if (existing.name == rule.name) {
            existing = std::move(rule);
            return;
        }
    }
    rules_.push_back(std::move(rule));
}

void RuleEngine::add_all(std::vector<Rule> rules) {
    for (auto& rule : rules) {
        add(std::move(rule));
    }
}

bool RuleEngine::contains(std::string_view name) const noexcept {
    return find(name).has_value();
}

std::optional<Rule> RuleEngine::find(std::string_view name) const {
    for (const auto& rule : rules_) {
        if (rule.name == name) {
            return rule;
        }
    }
    return std::nullopt;
}

std::shared_ptr<Law> RuleEngine::make_law(std::string_view name) const {
    auto rule = find(name);
    if (!rule.has_value()) {
        throw std::invalid_argument(fmt::format("unknown rule '{}'", name));
    }
    return std::make_shared<PatternLaw>(std::move(*rule));
}

const std::vector<Rule>& RuleEngine::rules() const noexcept {
    return rules_;
}

}  // namespace pv
