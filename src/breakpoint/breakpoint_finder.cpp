// SPDX-License-Identifier: Apache-2.0
#include "pv/breakpoint/breakpoint_finder.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include <fmt/format.h>

#include "pv/projection/entity_projection.hpp"
#include "pv/projection/projection_store.hpp"
#include "pv/projection/relation_projection.hpp"
#include "pv/projection/timeline_projection.hpp"
#include "pv/hash/hasher.hpp"
#include "pv/storage/repository.hpp"

namespace pv {

std::string_view to_string(BreakpointKind kind) noexcept {
    switch (kind) {
    case BreakpointKind::InvariantViolation:
        return "invariant_violation";
    case BreakpointKind::RepeatedRelation:
        return "repeated_relation";
    case BreakpointKind::AbnormalConcentration:
        return "abnormal_concentration";
    case BreakpointKind::BranchDivergence:
        return "branch_divergence";
    }
    return "repeated_relation";
}

std::string make_breakpoint_id(const Breakpoint& breakpoint) {
    auto sorted_evidence = breakpoint.evidence_ids;
    std::ranges::sort(sorted_evidence);
    sorted_evidence.erase(std::ranges::unique(sorted_evidence).begin(), sorted_evidence.end());

    CanonicalHasher hasher;
    hasher.write_string("Breakpoint:v1");
    hasher.write_string(breakpoint.branch);
    hasher.write_string(to_string(breakpoint.kind));
    hasher.write_hash(breakpoint.trigger.commit.value);
    hasher.write_u64(breakpoint.trigger.epoch.value);
    hasher.write_string(breakpoint.trigger.event);
    hasher.write_string(breakpoint.trigger.detail);
    hasher.write_string(breakpoint.trigger.evidence_event_id);
    hasher.write_string(breakpoint.trigger.from);
    hasher.write_string(breakpoint.trigger.to);
    hasher.write_string(breakpoint.trigger.relation);
    hasher.write_u64(breakpoint.trigger.pointer.value);
    for (const auto& evidence : sorted_evidence) {
        hasher.write_string(evidence);
    }
    return "bp_" + to_hex(hasher.finish()).substr(0, 16);
}

namespace {

struct RelationObservation {
    std::size_t commit_index{0};
    std::size_t event_index{0};
    CommitId commit;
    Epoch epoch;
    std::string event;
    std::string detail;
    std::string evidence_event_id;
    std::string from;
    std::string to;
    std::string relation;
    PointerId pointer;
};

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

PointerId pointer_id_from_field(const TraceEvent& event) {
    const auto value = field_or(event, "pointer");
    if (value.size() < 2 || value.front() != 'P') {
        return {};
    }
    try {
        return PointerId{std::stoull(value.substr(1))};
    } catch (const std::exception&) {
        return {};
    }
}

const ObjectSnapshot* object_by_name(const WorldSnapshot& snapshot, std::string_view name) {
    for (const auto& object : snapshot.objects) {
        if (object.name == name) {
            return &object;
        }
    }
    return nullptr;
}

std::string relation_detail(const TraceEvent& event) {
    const auto from = field_or(event, "from", "?");
    const auto to = field_or(event, "to", "?");
    const auto relation = field_or(event, "relation", "?");
    return fmt::format("{} -> {} : {}", from, to, relation);
}

std::vector<std::string> unique_sorted(std::vector<std::string> values) {
    std::ranges::sort(values);
    values.erase(std::ranges::unique(values).begin(), values.end());
    return values;
}

void append_unique(std::vector<std::string>& values, std::string value) {
    if (value.empty()) {
        return;
    }
    if (std::ranges::find(values, value) == values.end()) {
        values.push_back(std::move(value));
    }
}

void append_unique_pointer(std::vector<PointerId>& values, PointerId value) {
    if (!value.valid()) {
        return;
    }
    if (std::ranges::none_of(values, [&](PointerId id) { return id == value; })) {
        values.push_back(value);
    }
}

std::vector<RelationObservation> relation_observations(const std::vector<CommitRecord>& history) {
    std::vector<RelationObservation> observations;
    for (std::size_t commit_index = 0; commit_index < history.size(); ++commit_index) {
        const auto& record = history[commit_index];
        if (record.origin == TransactionOrigin::Internal) {
            continue;
        }

        std::vector<RelationObservation> graph_events;
        std::vector<RelationObservation> pointer_events;
        for (std::size_t event_index = 0; event_index < record.events.size(); ++event_index) {
            const auto& event = record.events[event_index];
            const auto relation = field_or(event, "relation");
            if (relation.empty()) {
                continue;
            }
            RelationObservation observation;
            observation.commit_index = commit_index;
            observation.event_index = event_index;
            observation.commit = record.id;
            observation.epoch = event.epoch;
            observation.event = event.event;
            observation.detail = relation_detail(event);
            observation.evidence_event_id = event_evidence_id(event);
            observation.from = field_or(event, "from");
            observation.to = field_or(event, "to");
            observation.relation = relation;
            observation.pointer = pointer_id_from_field(event);
            if (event.event == "graph.event") {
                graph_events.push_back(std::move(observation));
            } else if (event.event == "pointer.create") {
                pointer_events.push_back(std::move(observation));
            }
        }

        auto& selected = graph_events.empty() ? pointer_events : graph_events;
        observations.insert(
            observations.end(),
            std::make_move_iterator(selected.begin()),
            std::make_move_iterator(selected.end()));
    }
    return observations;
}

std::optional<PointerId> find_pointer_for(
    const WorldSnapshot& snapshot,
    const RelationObservation& observation) {
    if (observation.pointer.valid()) {
        return observation.pointer;
    }
    const auto* from = object_by_name(snapshot, observation.from);
    const auto* to = object_by_name(snapshot, observation.to);
    if (from == nullptr || to == nullptr) {
        return std::nullopt;
    }
    std::optional<PointerId> best;
    for (const auto& pointer : snapshot.pointers) {
        if (pointer.from != from->id || pointer.to != to->id) {
            continue;
        }
        if (snapshot.relation_name(pointer.relation) != observation.relation) {
            continue;
        }
        if (pointer.born_at.value > observation.epoch.value) {
            continue;
        }
        if (!best.has_value() || pointer.id.value > best->value) {
            best = pointer.id;
        }
    }
    return best;
}

std::optional<RelationObservation> observation_for_commit(
    const std::vector<RelationObservation>& observations,
    CommitId commit) {
    for (const auto& observation : observations) {
        if (observation.commit == commit) {
            return observation;
        }
    }
    return std::nullopt;
}

Breakpoint base_breakpoint(
    std::string_view branch,
    BreakpointKind kind,
    BreakpointTrigger trigger,
    std::string summary,
    std::vector<std::string> entities,
    std::vector<std::string> relations,
    std::vector<PointerId> pointers,
    std::vector<std::string> evidence) {
    Breakpoint breakpoint;
    breakpoint.kind = kind;
    breakpoint.branch = std::string{branch};
    breakpoint.summary = std::move(summary);
    breakpoint.trigger = std::move(trigger);
    breakpoint.affected_entities = unique_sorted(std::move(entities));
    breakpoint.affected_relations = unique_sorted(std::move(relations));
    breakpoint.affected_pointers = std::move(pointers);
    std::ranges::sort(breakpoint.affected_pointers, {}, &PointerId::value);
    breakpoint.affected_pointers.erase(
        std::ranges::unique(breakpoint.affected_pointers).begin(),
        breakpoint.affected_pointers.end());
    breakpoint.evidence_ids = unique_sorted(std::move(evidence));
    if (breakpoint.evidence_ids.empty()) {
        breakpoint.evidence_ids.push_back(commit_id(breakpoint.trigger.commit));
    }
    breakpoint.id = make_breakpoint_id(breakpoint);
    return breakpoint;
}

std::optional<Breakpoint> invariant_breakpoint(
    std::string_view branch,
    const CommitRecord& record,
    const std::vector<RelationObservation>& observations) {
    if (record.violations.empty()) {
        return std::nullopt;
    }

    BreakpointTrigger trigger;
    trigger.commit = record.id;
    trigger.epoch = record.after_epoch;
    trigger.event = "law.violation";
    trigger.detail = record.violations.front().law + ": " + record.violations.front().explanation;

    std::vector<std::string> entities;
    std::vector<std::string> relations;
    std::vector<PointerId> pointers;
    std::vector<std::string> evidence{commit_id(record.id)};
    for (const auto& violation : record.violations) {
        append_unique(evidence, violation.law);
        for (const auto& fact : violation.evidence) {
            append_unique(evidence, to_string(fact));
        }
        for (const auto pointer : violation.pointers) {
            append_unique_pointer(pointers, pointer);
        }
    }

    if (const auto observation = observation_for_commit(observations, record.id); observation.has_value()) {
        trigger.epoch = observation->epoch;
        trigger.event = observation->event;
        trigger.detail = observation->detail;
        trigger.evidence_event_id = observation->evidence_event_id;
        trigger.from = observation->from;
        trigger.to = observation->to;
        trigger.relation = observation->relation;
        trigger.pointer = observation->pointer;
        append_unique(evidence, observation->evidence_event_id);
        entities = {observation->from, observation->to};
        relations = {observation->relation};
        append_unique_pointer(pointers, observation->pointer);
    }

    return base_breakpoint(
        branch,
        BreakpointKind::InvariantViolation,
        std::move(trigger),
        fmt::format("declared invariant became violated at commit {}", short_commit(record.id)),
        std::move(entities),
        std::move(relations),
        std::move(pointers),
        std::move(evidence));
}

std::vector<Breakpoint> repeated_relation_breakpoints(
    std::string_view branch,
    const WorldSnapshot& snapshot,
    const std::vector<RelationObservation>& observations,
    std::size_t threshold) {
    std::vector<Breakpoint> out;
    std::map<std::string, std::vector<RelationObservation>> seen;
    std::set<std::string> emitted;
    for (const auto& observation : observations) {
        auto& prior = seen[observation.relation];
        prior.push_back(observation);
        if (prior.size() < threshold || emitted.contains(observation.relation)) {
            continue;
        }
        emitted.insert(observation.relation);

        BreakpointTrigger trigger;
        trigger.commit = observation.commit;
        trigger.epoch = observation.epoch;
        trigger.event = observation.event;
        trigger.detail = observation.detail;
        trigger.evidence_event_id = observation.evidence_event_id;
        trigger.from = observation.from;
        trigger.to = observation.to;
        trigger.relation = observation.relation;
        if (auto pointer = find_pointer_for(snapshot, observation); pointer.has_value()) {
            trigger.pointer = *pointer;
        }

        std::vector<std::string> evidence{commit_id(observation.commit)};
        std::vector<std::string> entities{observation.from, observation.to};
        std::vector<PointerId> pointers{trigger.pointer};
        for (const auto& item : prior) {
            append_unique(evidence, item.evidence_event_id);
            append_unique(evidence, commit_id(item.commit));
            append_unique(entities, item.from);
            append_unique(entities, item.to);
            if (auto pointer = find_pointer_for(snapshot, item); pointer.has_value()) {
                append_unique_pointer(pointers, *pointer);
            }
        }

        out.push_back(base_breakpoint(
            branch,
            BreakpointKind::RepeatedRelation,
            std::move(trigger),
            fmt::format("relation '{}' became repeated", observation.relation),
            std::move(entities),
            {observation.relation},
            std::move(pointers),
            std::move(evidence)));
    }
    return out;
}

std::vector<Breakpoint> concentration_breakpoints(
    std::string_view branch,
    const std::vector<RelationObservation>& observations,
    BreakpointFindOptions options) {
    std::vector<Breakpoint> out;
    std::map<std::string, std::size_t> counts;
    std::map<std::string, std::vector<RelationObservation>> enabling;
    std::set<std::string> emitted;
    std::size_t endpoint_events = 0;

    for (const auto& observation : observations) {
        for (const auto& entity : {observation.from, observation.to}) {
            if (entity.empty()) {
                continue;
            }
            endpoint_events += 1;
            counts[entity] += 1;
            enabling[entity].push_back(observation);
            const auto share = endpoint_events == 0
                ? 0.0
                : static_cast<double>(counts[entity]) / static_cast<double>(endpoint_events);
            if (emitted.contains(entity)
                || counts[entity] < options.concentration_min_events
                || share < options.concentration_share) {
                continue;
            }
            emitted.insert(entity);

            BreakpointTrigger trigger;
            trigger.commit = observation.commit;
            trigger.epoch = observation.epoch;
            trigger.event = observation.event;
            trigger.detail = fmt::format("{} concentrated {:.3f} of relation endpoints", entity, share);
            trigger.evidence_event_id = observation.evidence_event_id;
            trigger.from = observation.from;
            trigger.to = observation.to;
            trigger.relation = observation.relation;
            trigger.pointer = observation.pointer;

            std::vector<std::string> evidence{commit_id(observation.commit)};
            std::vector<std::string> relations;
            for (const auto& item : enabling[entity]) {
                append_unique(evidence, item.evidence_event_id);
                append_unique(evidence, commit_id(item.commit));
                append_unique(relations, item.relation);
            }

            out.push_back(base_breakpoint(
                branch,
                BreakpointKind::AbnormalConcentration,
                std::move(trigger),
                fmt::format("entity '{}' reached abnormal relation concentration", entity),
                {entity},
                std::move(relations),
                {observation.pointer},
                std::move(evidence)));
        }
    }
    return out;
}

std::optional<CommitRecord> record_by_commit(const std::vector<CommitRecord>& history, CommitId id) {
    for (const auto& record : history) {
        if (record.id == id) {
            return record;
        }
    }
    return std::nullopt;
}

std::vector<Breakpoint> branch_divergence_breakpoints(
    const ProjectionStore& store,
    std::string_view branch,
    const std::vector<CommitRecord>& history) {
    std::vector<Breakpoint> out;
    std::set<std::string> emitted;
    const auto& repository = store.repository();
    for (const auto& other : repository.list_branches()) {
        if (other.name == branch) {
            continue;
        }
        const auto analysis = repository.analyze_merge(branch, other.name);
        if (!analysis.left_divergence.commit.has_value()) {
            continue;
        }
        const auto commit = *analysis.left_divergence.commit;
        if (!emitted.insert(commit_id(commit)).second) {
            continue;
        }
        auto record = record_by_commit(history, commit);
        if (!record.has_value()) {
            continue;
        }

        BreakpointTrigger trigger;
        trigger.commit = record->id;
        trigger.epoch = record->after_epoch;
        trigger.event = "branch.divergence";
        trigger.detail = fmt::format(
            "{} diverged from {} after common ancestor {}",
            branch,
            other.name,
            analysis.common_ancestor.has_value() ? short_commit(*analysis.common_ancestor) : "none");

        out.push_back(base_breakpoint(
            branch,
            BreakpointKind::BranchDivergence,
            std::move(trigger),
            fmt::format("branch first diverged from '{}'", other.name),
            {},
            {},
            {},
            {commit_id(record->id)}));
    }
    return out;
}

std::unordered_map<std::string, std::size_t> commit_order(const std::vector<CommitRecord>& history) {
    std::unordered_map<std::string, std::size_t> order;
    for (std::size_t index = 0; index < history.size(); ++index) {
        order.emplace(commit_id(history[index].id), index);
    }
    return order;
}

std::uint8_t kind_rank(BreakpointKind kind) noexcept {
    switch (kind) {
    case BreakpointKind::InvariantViolation:
        return 0;
    case BreakpointKind::RepeatedRelation:
        return 1;
    case BreakpointKind::AbnormalConcentration:
        return 2;
    case BreakpointKind::BranchDivergence:
        return 3;
    }
    return 4;
}

void sort_breakpoints(std::vector<Breakpoint>& breakpoints, const std::vector<CommitRecord>& history) {
    const auto order = commit_order(history);
    std::ranges::sort(breakpoints, [&](const Breakpoint& left, const Breakpoint& right) {
        if (left.trigger.epoch.value != right.trigger.epoch.value) {
            return left.trigger.epoch.value < right.trigger.epoch.value;
        }
        const auto left_order = order.contains(commit_id(left.trigger.commit)) ? order.at(commit_id(left.trigger.commit)) : 0;
        const auto right_order = order.contains(commit_id(right.trigger.commit)) ? order.at(commit_id(right.trigger.commit)) : 0;
        if (left_order != right_order) {
            return left_order < right_order;
        }
        if (kind_rank(left.kind) != kind_rank(right.kind)) {
            return kind_rank(left.kind) < kind_rank(right.kind);
        }
        return left.id < right.id;
    });
}

}  // namespace

std::vector<Breakpoint> BreakpointFinder::find(
    const ProjectionStore& store,
    std::string_view branch,
    BreakpointFindOptions options) const {
    if (branch.empty()) {
        throw std::invalid_argument("breakpoint branch cannot be empty");
    }

    const auto timeline = TimelineProjection{}.project(store, branch);
    const auto entities = EntityProjection{}.project(store, branch);
    const auto relations = RelationProjection{}.project(store, branch);
    (void)timeline;
    (void)entities;
    (void)relations;

    const auto history = store.history(branch);
    const auto snapshot = store.snapshot(branch);
    const auto observations = relation_observations(history);

    std::vector<Breakpoint> breakpoints;
    for (const auto& record : history) {
        if (auto breakpoint = invariant_breakpoint(branch, record, observations); breakpoint.has_value()) {
            breakpoints.push_back(std::move(*breakpoint));
        }
    }
    auto repeated = repeated_relation_breakpoints(
        branch,
        snapshot,
        observations,
        std::max<std::size_t>(2, options.repeated_relation_threshold));
    breakpoints.insert(breakpoints.end(), std::make_move_iterator(repeated.begin()), std::make_move_iterator(repeated.end()));

    auto concentration = concentration_breakpoints(branch, observations, options);
    breakpoints.insert(
        breakpoints.end(),
        std::make_move_iterator(concentration.begin()),
        std::make_move_iterator(concentration.end()));

    if (options.include_branch_divergence) {
        auto divergence = branch_divergence_breakpoints(store, branch, history);
        breakpoints.insert(
            breakpoints.end(),
            std::make_move_iterator(divergence.begin()),
            std::make_move_iterator(divergence.end()));
    }

    sort_breakpoints(breakpoints, history);
    return breakpoints;
}

std::optional<Breakpoint> BreakpointFinder::find_by_id(
    const ProjectionStore& store,
    std::string_view branch,
    std::string_view breakpoint_id,
    BreakpointFindOptions options) const {
    for (auto breakpoint : find(store, branch, options)) {
        if (breakpoint.id == breakpoint_id) {
            return breakpoint;
        }
    }
    return std::nullopt;
}

std::string render_breakpoints_text(
    std::string_view branch,
    const std::vector<Breakpoint>& breakpoints) {
    std::ostringstream output;
    output << fmt::format("Breakpoints: {}\n", branch);
    output << "-------------\n";
    if (breakpoints.empty()) {
        output << "none\n";
        return output.str();
    }
    for (const auto& breakpoint : breakpoints) {
        output << fmt::format(
            "{} {} epoch {} commit {} evidence={} {}\n",
            breakpoint.id,
            to_string(breakpoint.kind),
            breakpoint.trigger.epoch.value,
            short_commit(breakpoint.trigger.commit),
            breakpoint.evidence_ids.size(),
            breakpoint.summary);
    }
    return output.str();
}

}  // namespace pv
