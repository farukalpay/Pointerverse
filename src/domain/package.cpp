// SPDX-License-Identifier: Apache-2.0
#include "pv/domain/package.hpp"

#include <cctype>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include <fmt/format.h>

#include "pv/domain/agent_audit.hpp"
#include "pv/rule/rule.hpp"

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

std::string strip_comment(std::string_view line) {
    const auto marker = line.find('#');
    return trim(marker == std::string_view::npos ? line : line.substr(0, marker));
}

}  // namespace

DomainPackage parse_domain_package(std::string_view text) {
    DomainPackage package;
    RuleBuilder builder;
    std::istringstream input{std::string{text}};
    std::string raw;
    while (std::getline(input, raw)) {
        const auto line = strip_comment(raw);
        if (line.empty()) {
            continue;
        }
        std::istringstream stream(line);
        std::string command;
        stream >> command;
        if (!builder.active() && (command == "domain" || command == "type" || command == "relation")) {
            std::string value;
            stream >> value;
            if (command == "domain") {
                package.name = value;
            } else if (command == "type" && !value.empty()) {
                package.schema.object_types.push_back(value);
            } else if (command == "relation" && !value.empty()) {
                package.schema.relations.push_back(value);
            }
            continue;
        }
        if (auto rule = builder.consume_line(line); rule.has_value()) {
            package.rules.push_back(std::move(*rule));
        }
    }
    if (builder.active()) {
        throw std::invalid_argument(fmt::format("domain package rule '{}' is incomplete", builder.name()));
    }
    return package;
}

DomainRegistry DomainRegistry::with_builtins() {
    DomainRegistry registry;
    registry.add(make_agent_audit_domain());
    return registry;
}

void DomainRegistry::add(DomainPackage package) {
    for (auto& existing : packages_) {
        if (existing.name == package.name) {
            existing = std::move(package);
            return;
        }
    }
    packages_.push_back(std::move(package));
}

std::optional<DomainPackage> DomainRegistry::find(std::string_view name) const {
    for (const auto& package : packages_) {
        if (package.name == name) {
            return package;
        }
    }
    return std::nullopt;
}

const std::vector<DomainPackage>& DomainRegistry::packages() const noexcept {
    return packages_;
}

}  // namespace pv
