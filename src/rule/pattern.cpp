// SPDX-License-Identifier: Apache-2.0
#include "pv/rule/pattern.hpp"

#include <variant>

namespace pv {
namespace {

bool wildcard_or_equal(const std::string& pattern, const std::string& value) {
    return pattern.empty() || pattern == "*" || pattern == value;
}

bool active_at(const PointerSnapshot& pointer, Epoch epoch) noexcept {
    return pointer.born_at <= epoch && (!pointer.expires_at.has_value() || epoch < *pointer.expires_at);
}

std::optional<ObjectId> object_named(const WorldSnapshot& snapshot, std::string_view name) {
    for (const auto& object : snapshot.objects) {
        if (object.name == name) {
            return object.id;
        }
    }
    return std::nullopt;
}

std::optional<ObjectId> resolve_ref_after(const LawCheckContext& ctx, const ObjectRef& ref) {
    if (const auto* id = std::get_if<ObjectId>(&ref)) {
        return ctx.after.contains(*id) ? std::optional<ObjectId>{*id} : std::nullopt;
    }

    const auto temp = std::get<TempObjectId>(ref);
    for (const auto& create : ctx.delta.creates) {
        if (create.temp_id == temp) {
            return object_named(ctx.after, create.name);
        }
    }
    return std::nullopt;
}

bool endpoint_allowed(PatternEndpointBinding binding, ObjectId candidate, const PatternMatch& trigger, bool from_side) {
    switch (binding) {
    case PatternEndpointBinding::Any:
        return true;
    case PatternEndpointBinding::TriggerFrom:
        return candidate == trigger.from;
    case PatternEndpointBinding::TriggerTo:
        return candidate == trigger.to;
    }
    return from_side ? candidate == trigger.from : candidate == trigger.to;
}

bool relation_exists_in_snapshot(
    const WorldSnapshot& snapshot,
    const PatternMatch& trigger,
    const RequirementPattern& requirement) {
    for (const auto& pointer : snapshot.pointers) {
        if (!active_at(pointer, snapshot.epoch)) {
            continue;
        }
        if (!endpoint_allowed(requirement.from_binding, pointer.from, trigger, true)
            || !endpoint_allowed(requirement.to_binding, pointer.to, trigger, false)) {
            continue;
        }
        if (matches_relation_pattern(snapshot, requirement.pattern, pointer.from, pointer.to, pointer.relation)) {
            return true;
        }
    }
    return false;
}

PatternMatch make_match(
    const WorldSnapshot& snapshot,
    ObjectId from,
    ObjectId to,
    RelationType relation) {
    const auto* from_object = snapshot.object(from);
    const auto* to_object = snapshot.object(to);
    return PatternMatch{
        from,
        to,
        from_object != nullptr ? from_object->name : to_string(from),
        to_object != nullptr ? to_object->name : to_string(to),
        snapshot.relation_name(relation)
    };
}

}  // namespace

bool matches_relation_pattern(
    const WorldSnapshot& snapshot,
    const RelationPattern& pattern,
    ObjectId from,
    ObjectId to,
    RelationType relation) {
    const auto* from_object = snapshot.object(from);
    const auto* to_object = snapshot.object(to);
    if (from_object == nullptr || to_object == nullptr) {
        return false;
    }

    return wildcard_or_equal(pattern.from_type, snapshot.type_name(from_object->type))
        && wildcard_or_equal(pattern.to_type, snapshot.type_name(to_object->type))
        && wildcard_or_equal(pattern.relation, snapshot.relation_name(relation));
}

std::vector<PatternMatch> trigger_matches(const LawCheckContext& ctx, const RelationPattern& trigger) {
    std::vector<PatternMatch> out;
    if (!ctx.delta.links.empty()) {
        for (const auto& link : ctx.delta.links) {
            const auto from = resolve_ref_after(ctx, link.from);
            const auto to = resolve_ref_after(ctx, link.to);
            if (!from.has_value() || !to.has_value()) {
                continue;
            }
            if (matches_relation_pattern(ctx.after, trigger, *from, *to, link.relation)) {
                out.push_back(make_match(ctx.after, *from, *to, link.relation));
            }
        }
        return out;
    }

    for (const auto& pointer : ctx.after.pointers) {
        if (!active_at(pointer, ctx.after.epoch)) {
            continue;
        }
        if (matches_relation_pattern(ctx.after, trigger, pointer.from, pointer.to, pointer.relation)) {
            out.push_back(make_match(ctx.after, pointer.from, pointer.to, pointer.relation));
        }
    }
    return out;
}

bool requirement_exists(
    const LawCheckContext& ctx,
    const PatternMatch& trigger,
    const RequirementPattern& requirement) {
    if (requirement.search == RequirementSearch::Before || requirement.search == RequirementSearch::BeforeOrAfter) {
        if (relation_exists_in_snapshot(ctx.before, trigger, requirement)) {
            return true;
        }
    }
    if (requirement.search == RequirementSearch::After || requirement.search == RequirementSearch::BeforeOrAfter) {
        return relation_exists_in_snapshot(ctx.after, trigger, requirement);
    }
    return false;
}

}  // namespace pv
