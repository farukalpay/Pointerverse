// SPDX-License-Identifier: Apache-2.0
#include "pv/breakpoint/evidence_chain.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <fmt/format.h>

#include "pv/projection/projection_store.hpp"

namespace pv {
namespace {

std::string commit_id(CommitId id) {
    return to_hex(id.value);
}

std::string short_commit(CommitId id) {
    return to_hex(id.value).substr(0, 12);
}

std::string field_or(const TraceEvent& event, std::string_view name, std::string fallback = {}) {
    const auto iter = event.fields.find(std::string{name});
    return iter == event.fields.end() ? std::move(fallback) : iter->second;
}

std::string event_evidence_id(const TraceEvent& event) {
    const auto source = field_or(event, "source");
    const auto id = field_or(event, "id", field_or(event, "event_id"));
    if (source.empty() || id.empty()) {
        return {};
    }
    return source + "/" + id;
}

std::string detail_for(const TraceEvent& event) {
    if (!field_or(event, "relation").empty()) {
        return fmt::format(
            "{} -> {} : {}",
            field_or(event, "from", "?"),
            field_or(event, "to", "?"),
            field_or(event, "relation", "?"));
    }
    if (!field_or(event, "object").empty()) {
        return field_or(event, "object");
    }
    return event.event;
}

bool contains(const std::vector<std::string>& values, std::string_view value) {
    return std::ranges::find(values, value) != values.end();
}

void append_unique(std::vector<std::string>& values, std::string value) {
    if (value.empty()) {
        return;
    }
    if (!contains(values, value)) {
        values.push_back(std::move(value));
    }
}

bool event_mentions_breakpoint(const TraceEvent& event, const Breakpoint& breakpoint) {
    const auto relation = field_or(event, "relation");
    if (!relation.empty() && contains(breakpoint.affected_relations, relation)) {
        return true;
    }
    for (const auto* field : {"from", "to", "actor", "subject", "object"}) {
        const auto entity = field_or(event, field);
        if (!entity.empty() && contains(breakpoint.affected_entities, entity)) {
            return true;
        }
    }
    return false;
}

bool same_trigger(const EvidenceChainStep& step, const Breakpoint& breakpoint) {
    return step.commit == breakpoint.trigger.commit
        && step.epoch == breakpoint.trigger.epoch
        && step.event == breakpoint.trigger.event
        && step.detail == breakpoint.trigger.detail;
}

std::vector<std::string> sorted_unique(std::vector<std::string> values) {
    std::ranges::sort(values);
    values.erase(std::ranges::unique(values).begin(), values.end());
    return values;
}

}  // namespace

EvidenceChain EvidenceChainBuilder::build(
    const ProjectionStore& store,
    std::string_view branch,
    const Breakpoint& breakpoint) const {
    if (breakpoint.id.empty()) {
        throw std::invalid_argument("cannot build evidence chain for an unnamed breakpoint");
    }
    if (breakpoint.evidence_ids.empty()) {
        throw std::invalid_argument("breakpoint has no evidence ids");
    }

    EvidenceChain chain;
    chain.breakpoint = breakpoint;
    chain.triggering_event = EvidenceChainStep{
        breakpoint.trigger.commit,
        breakpoint.trigger.epoch,
        breakpoint.trigger.event,
        breakpoint.trigger.detail,
        breakpoint.trigger.evidence_event_id.empty()
            ? commit_id(breakpoint.trigger.commit)
            : breakpoint.trigger.evidence_event_id
    };
    chain.affected_entities = breakpoint.affected_entities;
    chain.affected_relations = breakpoint.affected_relations;
    chain.evidence_ids = breakpoint.evidence_ids;

    for (const auto& record : store.history(branch)) {
        if (record.origin == TransactionOrigin::Internal) {
            continue;
        }
        const auto has_matching_graph_event = std::ranges::any_of(record.events, [&](const TraceEvent& event) {
            return event.event == "graph.event" && event_mentions_breakpoint(event, breakpoint);
        });
        for (const auto& event : record.events) {
            if (has_matching_graph_event && event.event != "graph.event" && !field_or(event, "relation").empty()) {
                continue;
            }
            EvidenceChainStep step{
                record.id,
                event.epoch,
                event.event,
                detail_for(event),
                event_evidence_id(event)
            };
            if (step.evidence_id.empty()) {
                step.evidence_id = commit_id(record.id);
            }
            if (same_trigger(step, breakpoint)) {
                continue;
            }
            if (step.epoch.value > breakpoint.trigger.epoch.value) {
                continue;
            }
            if (record.id == breakpoint.trigger.commit && step.epoch == breakpoint.trigger.epoch) {
                continue;
            }
            if (!event_mentions_breakpoint(event, breakpoint)) {
                continue;
            }
            chain.prior_enabling_events.push_back(std::move(step));
            append_unique(chain.evidence_ids, chain.prior_enabling_events.back().evidence_id);
        }
    }

    chain.evidence_ids = sorted_unique(std::move(chain.evidence_ids));
    return chain;
}

std::string render_evidence_chain_text(const EvidenceChain& chain) {
    std::ostringstream output;
    output << fmt::format("Evidence chain: {}\n", chain.breakpoint.id);
    output << "---------------\n";
    output << fmt::format("branch: {}\n", chain.breakpoint.branch);
    output << fmt::format("kind: {}\n", to_string(chain.breakpoint.kind));
    output << fmt::format("summary: {}\n", chain.breakpoint.summary);
    output << "triggering event:\n";
    output << fmt::format(
        "  epoch {} commit {} {} {} evidence={}\n",
        chain.triggering_event.epoch.value,
        short_commit(chain.triggering_event.commit),
        chain.triggering_event.event,
        chain.triggering_event.detail,
        chain.triggering_event.evidence_id);

    output << "prior enabling events:\n";
    if (chain.prior_enabling_events.empty()) {
        output << "  none\n";
    } else {
        for (const auto& step : chain.prior_enabling_events) {
            output << fmt::format(
                "  epoch {} commit {} {} {} evidence={}\n",
                step.epoch.value,
                short_commit(step.commit),
                step.event,
                step.detail,
                step.evidence_id);
        }
    }

    output << "affected entities:\n";
    if (chain.affected_entities.empty()) {
        output << "  none\n";
    } else {
        for (const auto& entity : chain.affected_entities) {
            output << fmt::format("  {}\n", entity);
        }
    }

    output << "affected relations:\n";
    if (chain.affected_relations.empty()) {
        output << "  none\n";
    } else {
        for (const auto& relation : chain.affected_relations) {
            output << fmt::format("  {}\n", relation);
        }
    }

    output << "evidence ids:\n";
    for (const auto& evidence : chain.evidence_ids) {
        output << fmt::format("  {}\n", evidence);
    }
    return output.str();
}

}  // namespace pv
