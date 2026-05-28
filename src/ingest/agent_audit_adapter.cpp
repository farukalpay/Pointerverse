// SPDX-License-Identifier: Apache-2.0
#include "pv/ingest/agent_audit_adapter.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <initializer_list>
#include <stdexcept>
#include <string_view>

namespace pv {
namespace {

bool action_is(std::string_view action, std::initializer_list<std::string_view> aliases) {
    return std::ranges::any_of(aliases, [action](std::string_view alias) {
        return action == alias;
    });
}

std::string first_value(const EvidenceEvent& event, std::initializer_list<std::string_view> names) {
    for (const auto name : names) {
        if (auto value = attribute_value(event, name); value.has_value() && !value->empty()) {
            return *value;
        }
    }
    return {};
}

std::string target_or_attr(const EvidenceEvent& event, std::initializer_list<std::string_view> names) {
    if (!event.target.empty()) {
        return event.target;
    }
    return first_value(event, names);
}

std::string require_value(std::string value, std::string_view field, const EvidenceEvent& event) {
    if (value.empty()) {
        throw std::invalid_argument(fmt::format(
            "event '{}' action '{}' requires {}",
            event.event_id,
            event.action,
            field));
    }
    return value;
}

NormalizedAuditEvent make_event(
    const EvidenceEvent& event,
    std::string from,
    std::string from_type,
    std::string relation,
    std::string to,
    std::string to_type) {
    return NormalizedAuditEvent{
        std::move(from),
        std::move(from_type),
        std::move(relation),
        std::move(to),
        std::move(to_type),
        event.actor,
        event.event_id,
        event.source,
        event.action,
        event.timestamp_ms
    };
}

}  // namespace

NormalizedAuditEvent AgentAuditAdapter::normalize(const EvidenceEvent& event) const {
    if (event.event_id.empty()) {
        throw std::invalid_argument("evidence event requires event_id");
    }
    if (event.source.empty()) {
        throw std::invalid_argument("evidence event requires source");
    }

    if (action_is(event.action, {"read_file", "tool.read_file"})) {
        return make_event(
            event,
            require_value(event.actor, "agent", event),
            "Agent",
            "reads",
            require_value(target_or_attr(event, {"path", "file", "target"}), "path", event),
            "File");
    }

    if (action_is(event.action, {"write_file", "modify_file", "tool.write_file"})) {
        return make_event(
            event,
            require_value(event.actor, "agent", event),
            "Agent",
            "modifies",
            require_value(target_or_attr(event, {"path", "file", "target"}), "path", event),
            "File");
    }

    if (action_is(event.action, {"create_pr", "github.create_pr"})) {
        return make_event(
            event,
            require_value(event.actor, "agent", event),
            "Agent",
            "creates",
            require_value(target_or_attr(event, {"pr", "pull_request", "target"}), "pr", event),
            "PullRequest");
    }

    if (action_is(event.action, {"test_passed", "ci.test_passed"})) {
        auto test = first_value(event, {"test", "test_run", "run", "target"});
        if (test.empty() && event.target_type == "TestRun") {
            test = event.target;
        }
        if (test.empty()) {
            test = "TestRun/" + event.event_id;
        }
        auto pull_request = first_value(event, {"pr", "pull_request"});
        if (pull_request.empty() && event.target_type == "PullRequest") {
            pull_request = event.target;
        }
        return make_event(
            event,
            require_value(std::move(pull_request), "pr", event),
            "PullRequest",
            "tests",
            std::move(test),
            "TestRun");
    }

    if (action_is(event.action, {"secret_detected", "secret.detected"})) {
        auto secret = target_or_attr(event, {"secret", "secret_id", "target"});
        if (secret.empty()) {
            secret = "Secret/" + event.event_id;
        }
        return make_event(
            event,
            require_value(event.actor, "agent", event),
            "Agent",
            "exposes",
            std::move(secret),
            "Secret");
    }

    if (action_is(event.action, {"policy_approved", "policy.approved"})) {
        auto policy = first_value(event, {"policy", "policy_id"});
        if (policy.empty()) {
            policy = event.actor;
        }
        return make_event(
            event,
            require_value(std::move(policy), "policy", event),
            "Policy",
            "approves",
            require_value(target_or_attr(event, {"resource", "target", "repo", "repository", "path"}), "resource", event),
            "Resource");
    }

    throw std::invalid_argument(fmt::format("unsupported agent audit action '{}'", event.action));
}

}  // namespace pv
