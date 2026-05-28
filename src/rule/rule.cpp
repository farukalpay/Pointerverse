// SPDX-License-Identifier: Apache-2.0
#include "pv/rule/rule.hpp"

#include <cctype>
#include <fmt/format.h>
#include <sstream>
#include <stdexcept>

namespace pv {
namespace {

std::string trim(std::string_view value) {
    auto begin = value.begin();
    auto end = value.end();
    while (begin != end && std::isspace(static_cast<unsigned char>(*begin))) {
        ++begin;
    }
    while (begin != end && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
        --end;
    }
    return std::string{begin, end};
}

bool starts_with(std::string_view value, std::string_view prefix) noexcept {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

std::string strip_comment(std::string_view line) {
    const auto marker = line.find('#');
    return trim(marker == std::string_view::npos ? line : line.substr(0, marker));
}

std::string quoted_reason(std::string_view line) {
    const auto first = line.find('"');
    const auto last = line.rfind('"');
    if (first == std::string_view::npos || last == first) {
        throw std::invalid_argument("deny reason requires a quoted string");
    }
    return std::string{line.substr(first + 1, last - first - 1)};
}

}  // namespace

bool RuleBuilder::active() const noexcept {
    return active_;
}

const std::string& RuleBuilder::name() const noexcept {
    return draft_.name;
}

std::optional<Rule> RuleBuilder::consume_line(std::string_view raw_line) {
    const auto line = strip_comment(raw_line);
    if (line.empty()) {
        return std::nullopt;
    }

    std::istringstream stream(line);
    std::string command;
    stream >> command;

    if (command == "rule") {
        std::string name;
        stream >> name;
        if (name.empty()) {
            throw std::invalid_argument("usage: rule NAME");
        }
        if (active_ && has_trigger_) {
            auto completed = draft_;
            draft_ = Rule{};
            draft_.name = name;
            active_ = true;
            has_trigger_ = false;
            return completed;
        }
        draft_ = Rule{};
        draft_.name = name;
        active_ = true;
        has_trigger_ = false;
        return std::nullopt;
    }

    if (!active_) {
        throw std::invalid_argument("rule clause found before rule NAME");
    }

    if (command == "when") {
        const auto prefix = std::string{"when "};
        draft_.trigger = parse_relation_pattern(line.substr(prefix.size()));
        has_trigger_ = true;
        return std::nullopt;
    }

    if (command == "require") {
        RequirementSearch search = RequirementSearch::BeforeOrAfter;
        std::string prefix{"require exists "};
        if (starts_with(line, "require before ")) {
            search = RequirementSearch::Before;
            prefix = "require before ";
        } else if (starts_with(line, "require after ")) {
            search = RequirementSearch::After;
            prefix = "require after ";
        } else if (!starts_with(line, prefix)) {
            throw std::invalid_argument("usage: require exists|before|after link FROM -> TO : RELATION");
        }
        RequirementPattern requirement;
        requirement.pattern = parse_relation_pattern(line.substr(prefix.size()));
        requirement.search = search;
        draft_.requirements.push_back(std::move(requirement));
        return std::nullopt;
    }

    if (command == "deny") {
        const auto prefix = std::string{"deny reason "};
        if (!starts_with(line, prefix)) {
            throw std::invalid_argument("usage: deny reason \"MESSAGE\"");
        }
        if (!has_trigger_) {
            throw std::invalid_argument("rule requires a when clause before deny");
        }
        draft_.reason = quoted_reason(line);
        auto completed = draft_;
        reset();
        return completed;
    }

    throw std::invalid_argument(fmt::format("unknown rule clause '{}'", command));
}

void RuleBuilder::reset() noexcept {
    draft_ = Rule{};
    active_ = false;
    has_trigger_ = false;
}

RelationPattern parse_relation_pattern(std::string_view text) {
    std::istringstream stream{std::string{text}};
    std::string link;
    std::string from;
    std::string arrow;
    std::string to;
    std::string colon;
    std::string relation;
    stream >> link >> from >> arrow >> to >> colon >> relation;
    if (link != "link" || from.empty() || arrow != "->" || to.empty() || colon != ":" || relation.empty()) {
        throw std::invalid_argument("usage: link FROM_TYPE -> TO_TYPE : RELATION");
    }
    return RelationPattern{from, relation, to};
}

std::vector<Rule> parse_rules(std::string_view text) {
    std::vector<Rule> out;
    RuleBuilder builder;
    std::istringstream input{std::string{text}};
    std::string line;
    while (std::getline(input, line)) {
        const auto clean = strip_comment(line);
        if (clean.empty()) {
            continue;
        }
        if (auto rule = builder.consume_line(clean); rule.has_value()) {
            out.push_back(std::move(*rule));
        }
    }
    if (builder.active()) {
        throw std::invalid_argument(fmt::format("rule '{}' is incomplete", builder.name()));
    }
    return out;
}

}  // namespace pv
