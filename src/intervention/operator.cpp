// SPDX-License-Identifier: Apache-2.0
#include "pv/intervention/operator.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

#include "pv/hash/hasher.hpp"
#include "pv/kernel/canonical_codec.hpp"

namespace pv {
namespace {

void sort_unique(std::vector<std::string>& values) {
    std::ranges::sort(values);
    values.erase(std::ranges::unique(values).begin(), values.end());
}

void write_strings(CanonicalWriter& writer, std::vector<std::string> values) {
    sort_unique(values);
    writer.u64(values.size());
    for (const auto& value : values) {
        writer.string(value);
    }
}

bool is_word_char(unsigned char ch) noexcept {
    return std::isalnum(ch) || ch == '_' || ch == '.' || ch == '/' || ch == '-';
}

std::uint64_t intervention_script_cost(std::string_view script) {
    std::vector<std::string> tokens;
    std::size_t index = 0;
    while (index < script.size()) {
        const auto ch = static_cast<unsigned char>(script[index]);
        if (std::isspace(ch)) {
            index += 1;
            continue;
        }
        if (script[index] == '#') {
            while (index < script.size() && script[index] != '\n') {
                index += 1;
            }
            continue;
        }
        if (script[index] == '-' && index + 1 < script.size() && script[index + 1] == '>') {
            tokens.emplace_back("->");
            index += 2;
            continue;
        }
        if (script[index] == ':' || script[index] == '=') {
            tokens.emplace_back(1, script[index]);
            index += 1;
            continue;
        }
        std::string token;
        while (index < script.size()) {
            const auto current = static_cast<unsigned char>(script[index]);
            if (!is_word_char(current)) {
                break;
            }
            if (script[index] == '-' && index + 1 < script.size() && script[index + 1] == '>') {
                break;
            }
            token.push_back(script[index]);
            index += 1;
        }
        if (!token.empty()) {
            tokens.push_back(std::move(token));
            continue;
        }
        tokens.emplace_back(1, script[index]);
        index += 1;
    }

    std::uint64_t size = 0;
    for (const auto& token : tokens) {
        if (size != 0) {
            size += 1;
        }
        size += token.size();
    }
    return size;
}

void write_trigger(CanonicalWriter& writer, const BreakpointTrigger& trigger) {
    writer.hash(trigger.commit.value);
    writer.u64(trigger.epoch.value);
    writer.string(trigger.event);
    writer.string(trigger.detail);
    writer.string(trigger.evidence_event_id);
    writer.string(trigger.from);
    writer.string(trigger.to);
    writer.string(trigger.relation);
    writer.u64(trigger.pointer.value);
}

}  // namespace

std::string_view to_string(InterventionKind kind) noexcept {
    switch (kind) {
    case InterventionKind::Identity:
        return "identity";
    case InterventionKind::ConstrainTriggeringRelation:
        return "constrain_triggering_relation";
    case InterventionKind::DelayTriggeringRelation:
        return "delay_triggering_relation";
    case InterventionKind::ReplaceTriggeringRelation:
        return "replace_triggering_relation";
    case InterventionKind::RemoveTriggeringRelation:
        return "remove_triggering_relation";
    }
    return "identity";
}

InterventionKind intervention_kind_for_repair(RepairAction action) noexcept {
    switch (action) {
    case RepairAction::ConstrainTriggeringRelation:
        return InterventionKind::ConstrainTriggeringRelation;
    case RepairAction::DelayTriggeringRelation:
        return InterventionKind::DelayTriggeringRelation;
    case RepairAction::ReplaceTriggeringRelation:
        return InterventionKind::ReplaceTriggeringRelation;
    case RepairAction::RemoveTriggeringRelation:
        return InterventionKind::RemoveTriggeringRelation;
    }
    return InterventionKind::RemoveTriggeringRelation;
}

RepairAction repair_action_for_intervention(InterventionKind kind) {
    switch (kind) {
    case InterventionKind::ConstrainTriggeringRelation:
        return RepairAction::ConstrainTriggeringRelation;
    case InterventionKind::DelayTriggeringRelation:
        return RepairAction::DelayTriggeringRelation;
    case InterventionKind::ReplaceTriggeringRelation:
        return RepairAction::ReplaceTriggeringRelation;
    case InterventionKind::RemoveTriggeringRelation:
        return RepairAction::RemoveTriggeringRelation;
    case InterventionKind::Identity:
        break;
    }
    throw std::invalid_argument("identity intervention has no repair action");
}

void encode(CanonicalWriter& writer, const InterventionOperator& op) {
    writer.string("InterventionOperator:v1");
    writer.string(op.breakpoint_id);
    writer.string(op.branch);
    writer.string(op.family);
    writer.string(std::string{to_string(op.kind)});
    encode(writer, op.scale);
    write_strings(writer, op.target_evidence_ids);
    write_strings(writer, op.target_relation_ids);
    write_strings(writer, op.target_entity_ids);
    write_trigger(writer, op.trigger);
    writer.u64(op.pointer.value);
    writer.u8(op.replacement_weight.has_value() ? 1 : 0);
    if (op.replacement_weight.has_value()) {
        writer.f64(*op.replacement_weight);
    }
    writer.string(op.replacement_relation);
    writer.u64(op.canonical_cost);
    writer.string(op.script);
}

Hash256 intervention_operator_hash(const InterventionOperator& op) {
    CanonicalWriter writer;
    encode(writer, op);
    return sha256(writer.bytes());
}

InterventionOperator canonicalize_intervention_operator(InterventionOperator op) {
    op.scale = ScaleValue::dyadic(op.scale.numerator, op.scale.exponent);
    sort_unique(op.target_evidence_ids);
    sort_unique(op.target_relation_ids);
    sort_unique(op.target_entity_ids);
    if (op.canonical_cost == 0 && !op.script.empty()) {
        op.canonical_cost = intervention_script_cost(op.script);
    }
    op.canonical_hash = intervention_operator_hash(op);
    op.id = to_hex(op.canonical_hash).substr(0, 12);
    return op;
}

InterventionOperator intervention_operator_from_repair(
    const RepairCandidate& candidate,
    ScaleValue scale) {
    InterventionOperator op;
    op.breakpoint_id = candidate.breakpoint_id;
    op.branch = candidate.branch;
    op.family = std::string{to_string(intervention_kind_for_repair(candidate.action))};
    op.kind = intervention_kind_for_repair(candidate.action);
    op.scale = scale;
    op.target_evidence_ids = candidate.evidence_ids;
    op.target_relation_ids = candidate.trigger.relation.empty()
        ? std::vector<std::string>{}
        : std::vector<std::string>{candidate.trigger.relation};
    op.target_entity_ids = {};
    if (!candidate.trigger.from.empty()) {
        op.target_entity_ids.push_back(candidate.trigger.from);
    }
    if (!candidate.trigger.to.empty()) {
        op.target_entity_ids.push_back(candidate.trigger.to);
    }
    op.trigger = candidate.trigger;
    op.pointer = candidate.pointer;
    op.replacement_weight = candidate.replacement_weight;
    op.replacement_relation = candidate.replacement_relation;
    op.script = candidate.script;
    op.canonical_cost = intervention_script_cost(candidate.script);
    return canonicalize_intervention_operator(std::move(op));
}

RepairCandidate repair_candidate_from_operator(const InterventionOperator& op) {
    RepairCandidate candidate;
    candidate.breakpoint_id = op.breakpoint_id;
    candidate.branch = op.branch;
    candidate.action = repair_action_for_intervention(op.kind);
    candidate.trigger = op.trigger;
    candidate.pointer = op.pointer;
    candidate.evidence_ids = op.target_evidence_ids;
    candidate.replacement_weight = op.replacement_weight;
    candidate.replacement_relation = op.replacement_relation;
    candidate.script = op.script;
    return candidate;
}

}  // namespace pv
