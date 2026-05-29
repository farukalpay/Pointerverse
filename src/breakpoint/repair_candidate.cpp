// SPDX-License-Identifier: Apache-2.0
#include "pv/breakpoint/repair_candidate.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>

#include <fmt/format.h>

#include "pv/projection/projection_store.hpp"

namespace pv {
namespace {

bool active_at(const PointerSnapshot& pointer, Epoch epoch) noexcept {
    return pointer.born_at <= epoch && (!pointer.expires_at.has_value() || epoch < *pointer.expires_at);
}

const ObjectSnapshot* object_by_name(const WorldSnapshot& snapshot, std::string_view name) {
    for (const auto& object : snapshot.objects) {
        if (object.name == name) {
            return &object;
        }
    }
    return nullptr;
}

std::string object_name(const WorldSnapshot& snapshot, ObjectId id) {
    if (const auto* object = snapshot.object(id); object != nullptr) {
        return object->name;
    }
    return to_string(id);
}

const PointerSnapshot* pointer_from_breakpoint(const WorldSnapshot& snapshot, const Breakpoint& breakpoint) {
    if (breakpoint.trigger.pointer.valid()) {
        if (const auto* pointer = snapshot.pointer(breakpoint.trigger.pointer); pointer != nullptr) {
            return pointer;
        }
    }
    for (const auto pointer_id : breakpoint.affected_pointers) {
        if (const auto* pointer = snapshot.pointer(pointer_id); pointer != nullptr) {
            return pointer;
        }
    }
    if (breakpoint.trigger.from.empty() || breakpoint.trigger.to.empty() || breakpoint.trigger.relation.empty()) {
        return nullptr;
    }

    const auto* from = object_by_name(snapshot, breakpoint.trigger.from);
    const auto* to = object_by_name(snapshot, breakpoint.trigger.to);
    if (from == nullptr || to == nullptr) {
        return nullptr;
    }

    const PointerSnapshot* best = nullptr;
    for (const auto& pointer : snapshot.pointers) {
        if (pointer.from != from->id || pointer.to != to->id) {
            continue;
        }
        if (snapshot.relation_name(pointer.relation) != breakpoint.trigger.relation) {
            continue;
        }
        if (!active_at(pointer, snapshot.epoch)) {
            continue;
        }
        if (best == nullptr || pointer.id.value > best->id.value) {
            best = &pointer;
        }
    }
    return best;
}

RepairAction action_for(const Breakpoint& breakpoint, const PointerSnapshot& pointer) {
    if (breakpoint.kind == BreakpointKind::InvariantViolation && pointer.weight.value > 1.0) {
        return RepairAction::ConstrainTriggeringRelation;
    }
    if (breakpoint.kind == BreakpointKind::AbnormalConcentration) {
        return RepairAction::ConstrainTriggeringRelation;
    }
    return RepairAction::RemoveTriggeringRelation;
}

std::string script_for(
    const Breakpoint& breakpoint,
    RepairAction action,
    const WorldSnapshot& snapshot,
    const PointerSnapshot& pointer) {
    const auto from = object_name(snapshot, pointer.from);
    const auto to = object_name(snapshot, pointer.to);
    const auto relation = snapshot.relation_name(pointer.relation);

    std::ostringstream script;
    script << "# pointerverse repair candidate v1\n";
    script << fmt::format("# breakpoint {}\n", breakpoint.id);
    for (const auto& evidence : breakpoint.evidence_ids) {
        script << fmt::format("# evidence {}\n", evidence);
    }

    if (action == RepairAction::ConstrainTriggeringRelation) {
        const auto weight = pointer.weight.value > 1.0 ? 1.0 : std::max(0.0, pointer.weight.value * 0.5);
        script << fmt::format(
            "constrain {} -> {} : {} weight={:.12g} pointer=P{}\n",
            from,
            to,
            relation,
            weight,
            pointer.id.value);
    } else {
        script << fmt::format(
            "unlink {} -> {} : {} pointer=P{}\n",
            from,
            to,
            relation,
            pointer.id.value);
    }
    return script.str();
}

}  // namespace

std::string_view to_string(RepairAction action) noexcept {
    switch (action) {
    case RepairAction::RemoveTriggeringRelation:
        return "remove_triggering_relation";
    case RepairAction::ConstrainTriggeringRelation:
        return "constrain_triggering_relation";
    case RepairAction::DelayTriggeringRelation:
        return "delay_triggering_relation";
    case RepairAction::ReplaceTriggeringRelation:
        return "replace_triggering_relation";
    }
    return "remove_triggering_relation";
}

RepairCandidate RepairCandidateBuilder::build(
    const ProjectionStore& store,
    std::string_view branch,
    const Breakpoint& breakpoint) const {
    if (breakpoint.evidence_ids.empty()) {
        throw std::invalid_argument("cannot recommend repair without evidence ids");
    }
    if (breakpoint.trigger.event.empty() || !breakpoint.trigger.commit.valid()) {
        throw std::invalid_argument("cannot build repair candidate without a concrete triggering event");
    }

    const auto snapshot = store.snapshot(branch);
    const auto* pointer = pointer_from_breakpoint(snapshot, breakpoint);
    if (pointer == nullptr) {
        throw std::invalid_argument("breakpoint has no concrete triggering relation in the branch snapshot");
    }

    RepairCandidate candidate;
    candidate.breakpoint_id = breakpoint.id;
    candidate.branch = std::string{branch};
    candidate.action = action_for(breakpoint, *pointer);
    candidate.trigger = breakpoint.trigger;
    candidate.pointer = pointer->id;
    candidate.evidence_ids = breakpoint.evidence_ids;
    candidate.script = script_for(breakpoint, candidate.action, snapshot, *pointer);
    return candidate;
}

}  // namespace pv
