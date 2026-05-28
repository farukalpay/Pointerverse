// SPDX-License-Identifier: Apache-2.0
#include "pv/domain/agent_audit.hpp"

#include <utility>

namespace pv {
namespace {

Rule same_endpoint_rule(
    std::string name,
    RelationPattern trigger,
    RelationPattern requirement,
    std::string reason,
    RequirementSearch search = RequirementSearch::BeforeOrAfter) {
    Rule rule;
    rule.name = std::move(name);
    rule.trigger = std::move(trigger);
    RequirementPattern required;
    required.pattern = std::move(requirement);
    required.search = search;
    rule.requirements.push_back(std::move(required));
    rule.reason = std::move(reason);
    return rule;
}

Rule no_requirement_rule(std::string name, RelationPattern trigger, std::string reason) {
    Rule rule;
    rule.name = std::move(name);
    rule.trigger = std::move(trigger);
    rule.reason = std::move(reason);
    return rule;
}

}  // namespace

DomainPackage make_agent_audit_domain() {
    DomainPackage package;
    package.name = "agent_audit";
    package.schema.object_types = {
        "Agent",
        "Tool",
        "Resource",
        "File",
        "Repository",
        "PullRequest",
        "TestRun",
        "Secret",
        "Evidence",
        "Action",
        "Policy"
    };
    package.schema.relations = {
        "calls",
        "reads",
        "writes",
        "modifies",
        "creates",
        "depends_on",
        "contains",
        "exposes",
        "approves",
        "backs",
        "tests",
        "touches"
    };

    package.rules.push_back(same_endpoint_rule(
        "no_write_without_read",
        RelationPattern{"Agent", "modifies", "File"},
        RelationPattern{"Agent", "reads", "File"},
        "{from} modifies {to} without prior read relation",
        RequirementSearch::Before));

    Rule no_pr;
    no_pr.name = "no_pr_without_tests";
    no_pr.trigger = RelationPattern{"Agent", "creates", "PullRequest"};
    no_pr.requirements.push_back(RequirementPattern{
        RelationPattern{"PullRequest", "tests", "TestRun"},
        PatternEndpointBinding::TriggerTo,
        PatternEndpointBinding::Any,
        RequirementSearch::BeforeOrAfter
    });
    no_pr.reason = "{from} creates {to} without a TestRun relation";
    package.rules.push_back(std::move(no_pr));

    package.rules.push_back(no_requirement_rule(
        "no_secret_exposure",
        RelationPattern{"Agent", "exposes", "Secret"},
        "{from} exposes secret {to}"));

    Rule no_orphan;
    no_orphan.name = "no_orphan_action";
    no_orphan.trigger = RelationPattern{"Agent", "creates", "Action"};
    no_orphan.requirements.push_back(RequirementPattern{
        RelationPattern{"Action", "depends_on", "Policy"},
        PatternEndpointBinding::TriggerTo,
        PatternEndpointBinding::Any,
        RequirementSearch::BeforeOrAfter
    });
    no_orphan.reason = "{to} is an orphan action without a policy dependency";
    package.rules.push_back(std::move(no_orphan));

    Rule no_external;
    no_external.name = "no_unapproved_external_write";
    no_external.trigger = RelationPattern{"Agent", "writes", "Resource"};
    no_external.requirements.push_back(RequirementPattern{
        RelationPattern{"Policy", "approves", "Resource"},
        PatternEndpointBinding::Any,
        PatternEndpointBinding::TriggerTo,
        RequirementSearch::BeforeOrAfter
    });
    no_external.reason = "{from} writes {to} without policy approval";
    package.rules.push_back(std::move(no_external));

    return package;
}

}  // namespace pv
