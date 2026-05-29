// SPDX-License-Identifier: Apache-2.0
#include "pv/measure/counterfactual_measure.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <variant>

#include <fmt/format.h>

#include "pv/measure/intrinsic_edit_cost.hpp"
#include "pv/projection/projection_store.hpp"
#include "pv/storage/object_codec.hpp"
#include "pv/storage/repository.hpp"

namespace pv {
namespace {

struct DelayedRelation {
    std::string from;
    std::string to;
    std::string relation;
    CausalRole role{CausalRole::Structural};
    double weight{1.0};
    std::string law_domain{"core"};
    std::optional<TraceEvent> graph_event;
};

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

std::uint64_t next_pointer_value(const WorldSnapshot& snapshot) noexcept {
    std::uint64_t next = 1;
    for (const auto& pointer : snapshot.pointers) {
        next = std::max(next, pointer.id.value + 1);
    }
    return next;
}

std::uint32_t next_relation_value(const WorldSnapshot& snapshot, const Delta& delta) noexcept {
    std::uint32_t next = 1;
    for (const auto& [id, _] : snapshot.relation_names) {
        next = std::max(next, id + 1);
    }
    for (const auto& op : delta.ops) {
        if (op.kind == OperationKind::InternRelation) {
            next = std::max(next, std::get<InternRelationOp>(op.body).id.id + 1);
        }
    }
    return next;
}

std::optional<RelationType> relation_named(const WorldSnapshot& snapshot, std::string_view name) {
    for (const auto& [id, candidate] : snapshot.relation_names) {
        if (candidate == name) {
            return RelationType{id};
        }
    }
    return std::nullopt;
}

std::optional<std::string> object_name_for_ref(
    const WorldSnapshot& before,
    const std::map<std::uint32_t, std::string>& temp_objects,
    const ObjectRef& ref) {
    if (const auto* object = std::get_if<ObjectId>(&ref)) {
        const auto* snapshot = before.object(*object);
        if (snapshot == nullptr) {
            return std::nullopt;
        }
        return snapshot->name;
    }
    const auto temp = std::get<TempObjectId>(ref);
    const auto iter = temp_objects.find(temp.value);
    if (iter == temp_objects.end()) {
        return std::nullopt;
    }
    return iter->second;
}

bool same_relation_set(std::vector<std::string> left, std::vector<std::string> right) {
    std::ranges::sort(left);
    std::ranges::sort(right);
    return left == right;
}

bool overlapping_entities(const std::vector<std::string>& left, const std::vector<std::string>& right) {
    return std::ranges::any_of(left, [&](const auto& entity) {
        return std::ranges::find(right, entity) != right.end();
    });
}

bool graph_event_matches(const TraceEvent& event, const RepairCandidate& candidate) {
    if (event.event != "graph.event") {
        return false;
    }
    const auto evidence = event_evidence_id(event);
    if (!candidate.trigger.evidence_event_id.empty() && evidence == candidate.trigger.evidence_event_id) {
        return true;
    }
    return field_or(event, "from") == candidate.trigger.from
        && field_or(event, "to") == candidate.trigger.to
        && field_or(event, "relation") == candidate.trigger.relation;
}

bool pointer_create_matches(
    PointerId predicted,
    const WorldSnapshot& before,
    const std::map<std::uint32_t, std::string>& temp_objects,
    const std::map<std::uint32_t, std::string>& relations,
    const CreatePointerOp& create,
    const RepairCandidate& candidate) {
    if (candidate.pointer.valid() && predicted == candidate.pointer) {
        return true;
    }
    const auto from = object_name_for_ref(before, temp_objects, create.from);
    const auto to = object_name_for_ref(before, temp_objects, create.to);
    const auto relation = relations.find(create.relation.id);
    return from.has_value()
        && to.has_value()
        && relation != relations.end()
        && *from == candidate.trigger.from
        && *to == candidate.trigger.to
        && relation->second == candidate.trigger.relation;
}

bool pointer_op_targets_skipped(const Operation& op, PointerId skipped) {
    if (!skipped.valid()) {
        return false;
    }
    switch (op.kind) {
    case OperationKind::ExpirePointer:
        return std::get<ExpirePointerOp>(op.body).id == skipped;
    case OperationKind::SetPointerWeight:
        return std::get<SetPointerWeightOp>(op.body).id == skipped;
    case OperationKind::SetPointerAttribute:
        return std::get<SetPointerAttributeOp>(op.body).id == skipped;
    case OperationKind::RemovePointerAttribute:
        return std::get<RemovePointerAttributeOp>(op.body).id == skipped;
    case OperationKind::AssertPointer:
        return std::get<AssertPointerOp>(op.body).id == skipped;
    default:
        return false;
    }
}

void append_reindexed(Delta& delta, Operation op) {
    op.id = {};
    delta.append(std::move(op));
}

struct EditedDelta {
    Delta delta;
    std::optional<DelayedRelation> delayed;
    bool changed{false};
};

EditedDelta edit_delta(
    const WorldSnapshot& before,
    const Delta& original,
    const RepairCandidate& candidate) {
    EditedDelta edited;
    std::map<std::uint32_t, std::string> temp_objects;
    std::map<std::uint32_t, std::string> relations = before.relation_names;
    PointerId skipped_pointer;
    std::uint64_t next_pointer = next_pointer_value(before);

    RelationType replacement_relation;
    if (candidate.action == RepairAction::ReplaceTriggeringRelation) {
        if (auto existing = relation_named(before, candidate.replacement_relation); existing.has_value()) {
            replacement_relation = *existing;
        } else {
            replacement_relation = RelationType{next_relation_value(before, original)};
            edited.delta.append_intern_relation(candidate.replacement_relation, replacement_relation);
            relations.emplace(replacement_relation.id, candidate.replacement_relation);
        }
    }

    for (const auto& op : original.ops) {
        if (op.kind == OperationKind::CreateObject) {
            const auto& create = std::get<CreateObjectOp>(op.body);
            temp_objects.emplace(create.temp_id.value, create.name);
            append_reindexed(edited.delta, op);
            continue;
        }
        if (op.kind == OperationKind::InternRelation) {
            const auto& intern = std::get<InternRelationOp>(op.body);
            relations.emplace(intern.id.id, intern.name);
            append_reindexed(edited.delta, op);
            continue;
        }
        if (pointer_op_targets_skipped(op, skipped_pointer)) {
            edited.changed = true;
            continue;
        }
        if (op.kind == OperationKind::CreatePointer) {
            auto create = std::get<CreatePointerOp>(op.body);
            const auto predicted = PointerId{next_pointer++};
            if (pointer_create_matches(predicted, before, temp_objects, relations, create, candidate)) {
                edited.changed = true;
                if (candidate.action == RepairAction::RemoveTriggeringRelation) {
                    skipped_pointer = predicted;
                    continue;
                }
                if (candidate.action == RepairAction::DelayTriggeringRelation) {
                    skipped_pointer = predicted;
                    const auto from = object_name_for_ref(before, temp_objects, create.from);
                    const auto to = object_name_for_ref(before, temp_objects, create.to);
                    const auto relation = relations.find(create.relation.id);
                    if (from.has_value() && to.has_value() && relation != relations.end()) {
                        edited.delayed = DelayedRelation{
                            *from,
                            *to,
                            relation->second,
                            create.causal_role,
                            create.weight.value,
                            create.law_domain,
                            std::nullopt
                        };
                    }
                    continue;
                }
                if (candidate.action == RepairAction::ConstrainTriggeringRelation) {
                    create.weight = Weight{candidate.replacement_weight.value_or(create.weight.value)};
                }
                if (candidate.action == RepairAction::ReplaceTriggeringRelation) {
                    create.relation = replacement_relation;
                }
            }
            append_reindexed(edited.delta, Operation{{}, OperationKind::CreatePointer, std::move(create)});
            continue;
        }
        if (op.kind == OperationKind::EmitEvent) {
            auto event = std::get<EmitEventOp>(op.body).event;
            if (graph_event_matches(event, candidate)) {
                edited.changed = true;
                if (candidate.action == RepairAction::RemoveTriggeringRelation) {
                    continue;
                }
                if (candidate.action == RepairAction::DelayTriggeringRelation) {
                    if (edited.delayed.has_value()) {
                        edited.delayed->graph_event = event;
                    }
                    continue;
                }
                if (candidate.action == RepairAction::ReplaceTriggeringRelation) {
                    event.fields["relation"] = candidate.replacement_relation;
                }
            }
            append_reindexed(edited.delta, Operation{{}, OperationKind::EmitEvent, EmitEventOp{std::move(event)}});
            continue;
        }
        append_reindexed(edited.delta, op);
    }

    return edited;
}

std::filesystem::path temp_repository_path() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("pointerverse_counterfactual_" + std::to_string(stamp));
}

void commit_delta(
    Repository& repository,
    std::string_view branch,
    Delta delta,
    std::string label,
    const Verifier& verifier) {
    if (delta.empty()) {
        return;
    }
    Transaction tx;
    tx.origin = TransactionOrigin::Replay;
    tx.label = std::move(label);
    tx.delta = std::move(delta);
    const auto record = repository.commit(branch, std::move(tx), verifier);
    if (!record.has_value() || !record->accepted) {
        throw std::runtime_error("counterfactual replay commit was rejected");
    }
}

void append_delayed(
    Repository& repository,
    std::string_view branch,
    const DelayedRelation& delayed,
    const Verifier& verifier) {
    auto& world = repository.mutable_world(branch);
    Delta delta;
    delta.append_link(PointerCreate{
        ObjectRef{world.object_by_name(delayed.from)},
        ObjectRef{world.object_by_name(delayed.to)},
        world.relation_type(delayed.relation),
        delayed.role,
        Weight{delayed.weight},
        delayed.law_domain,
        {}
    });
    auto event = delayed.graph_event.value_or(TraceEvent{
        {},
        "graph.event",
        {
            {"source", "counterfactual"},
            {"id", "delayed"},
            {"from", delayed.from},
            {"relation", delayed.relation},
            {"to", delayed.to}
        },
        {}
    });
    event.epoch = {};
    event.fields["from"] = delayed.from;
    event.fields["to"] = delayed.to;
    event.fields["relation"] = delayed.relation;
    delta.append_event(std::move(event));
    commit_delta(repository, branch, std::move(delta), "counterfactual delay", verifier);
}

std::vector<Breakpoint> replay_breakpoints(
    const Repository& source,
    std::string_view branch,
    const RepairCandidate& candidate,
    const Verifier& verifier,
    BreakpointFindOptions find_options,
    bool& changed) {
    const auto history = source.backend().history(branch);
    if (history.empty()) {
        throw std::runtime_error("cannot replay an empty branch history");
    }

    const auto temp_root = temp_repository_path();
    std::filesystem::remove_all(temp_root);
    auto cleanup = [&] {
        std::error_code ignored;
        std::filesystem::remove_all(temp_root, ignored);
    };

    auto repository = Repository::init(temp_root);
    const auto genesis = source.backend().snapshot(history.front().id);
    (void)repository.create_branch(std::string{branch}, World::from_snapshot(genesis));

    std::optional<DelayedRelation> delayed;
    try {
        for (std::size_t index = 1; index < history.size(); ++index) {
            const auto& record = history[index];
            if (record.origin == TransactionOrigin::Internal) {
                continue;
            }
            const auto stored = source.backend().stored_commit(record.id);
            const auto original = source.objects().get_canonical<Delta>(stored.delta_object);
            const auto before = repository.world(branch).snapshot();
            auto edited = record.id == candidate.trigger.commit
                ? edit_delta(before, original, candidate)
                : EditedDelta{original, std::nullopt, false};
            changed = changed || edited.changed;
            if (edited.delayed.has_value()) {
                delayed = std::move(edited.delayed);
            }
            commit_delta(repository, branch, std::move(edited.delta), record.label, verifier);
        }
        if (delayed.has_value()) {
            append_delayed(repository, branch, *delayed, verifier);
        }

        ProjectionStore store{repository};
        auto breakpoints = BreakpointFinder{}.find(store, branch, find_options);
        cleanup();
        return breakpoints;
    } catch (...) {
        cleanup();
        throw;
    }
}

}  // namespace

bool equivalent_breakpoint(const Breakpoint& original, const Breakpoint& candidate) {
    return original.kind == candidate.kind
        && same_relation_set(original.affected_relations, candidate.affected_relations)
        && overlapping_entities(original.affected_entities, candidate.affected_entities);
}

std::optional<Breakpoint> surviving_breakpoint(
    const Breakpoint& original,
    const std::vector<Breakpoint>& repaired) {
    const auto iter = std::ranges::find_if(repaired, [&](const Breakpoint& candidate) {
        return equivalent_breakpoint(original, candidate);
    });
    if (iter == repaired.end()) {
        return std::nullopt;
    }
    return *iter;
}

CounterfactualRepairMeasure CounterfactualMeasure::evaluate(
    const Repository& repository,
    std::string_view branch,
    const Breakpoint& breakpoint,
    const RepairCandidate& candidate,
    const Verifier* verifier,
    BreakpointFindOptions find_options) const {
    CounterfactualRepairMeasure result;
    result.candidate = candidate;
    const auto cost = IntrinsicEditCost{}.measure(candidate.script);
    result.canonical_cost = cost.value;
    result.canonical_script = cost.canonical_script;

    Verifier default_verifier;
    const auto& replay_verifier = verifier == nullptr ? default_verifier : *verifier;
    try {
        bool changed = false;
        const auto repaired = replay_breakpoints(
            repository,
            branch,
            candidate,
            replay_verifier,
            find_options,
            changed);
        result.replayed = changed;
        result.survivor = surviving_breakpoint(breakpoint, repaired);
        result.survives = result.survivor.has_value();
        result.explanation = result.survives
            ? "equivalent breakpoint survived counterfactual replay"
            : "equivalent breakpoint did not survive counterfactual replay";
        if (!changed) {
            result.explanation = "candidate did not edit the replayed history";
        }
    } catch (const std::exception& error) {
        result.replayed = false;
        result.survives = true;
        result.explanation = fmt::format("counterfactual replay failed: {}", error.what());
    }
    return result;
}

}  // namespace pv
