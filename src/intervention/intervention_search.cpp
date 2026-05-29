// SPDX-License-Identifier: Apache-2.0
#include "pv/intervention/intervention_search.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <variant>

#include <fmt/format.h>

#include "pv/hash/hasher.hpp"
#include "pv/kernel/canonical_codec.hpp"
#include "pv/projection/projection_store.hpp"
#include "pv/intervention/refinement_tree.hpp"
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

struct EditedDelta {
    Delta delta;
    std::vector<DelayedRelation> delayed;
    bool changed{false};
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
                        edited.delayed.push_back(DelayedRelation{
                            *from,
                            *to,
                            relation->second,
                            create.causal_role,
                            create.weight.value,
                            create.law_domain,
                            std::nullopt
                        });
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
                    if (!edited.delayed.empty()) {
                        edited.delayed.back().graph_event = event;
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

EditedDelta edit_delta_for_program(
    const WorldSnapshot& before,
    const Delta& original,
    const InterventionProgram& program,
    CommitId commit) {
    EditedDelta current{original, {}, false};
    for (const auto& op : program.operators) {
        if (op.kind == InterventionKind::Identity || op.trigger.commit != commit) {
            continue;
        }
        const auto candidate = repair_candidate_from_operator(op);
        auto edited = edit_delta(before, current.delta, candidate);
        current.changed = current.changed || edited.changed;
        current.delta = std::move(edited.delta);
        current.delayed.insert(
            current.delayed.end(),
            std::make_move_iterator(edited.delayed.begin()),
            std::make_move_iterator(edited.delayed.end()));
    }
    return current;
}

std::filesystem::path temp_repository_path() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("pointerverse_intervention_" + std::to_string(stamp));
}

std::optional<CommitRecord> commit_delta(
    Repository& repository,
    std::string_view branch,
    Delta delta,
    std::string label,
    const Verifier& verifier) {
    if (delta.empty()) {
        return std::nullopt;
    }
    Transaction tx;
    tx.origin = TransactionOrigin::Replay;
    tx.label = std::move(label);
    tx.delta = std::move(delta);
    const auto record = repository.commit(branch, std::move(tx), verifier);
    if (!record.has_value() || !record->accepted) {
        throw std::runtime_error("intervention replay commit was rejected");
    }
    return record;
}

std::optional<CommitRecord> append_delayed(
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
            {"source", "intervention"},
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
    return commit_delta(repository, branch, std::move(delta), "intervention delay", verifier);
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

bool equivalent_breakpoint_local(const Breakpoint& original, const Breakpoint& candidate) {
    return original.kind == candidate.kind
        && same_relation_set(original.affected_relations, candidate.affected_relations)
        && overlapping_entities(original.affected_entities, candidate.affected_entities);
}

std::optional<Breakpoint> surviving_breakpoint_local(
    const Breakpoint& original,
    const std::vector<Breakpoint>& repaired) {
    const auto iter = std::ranges::find_if(repaired, [&](const Breakpoint& candidate) {
        return equivalent_breakpoint_local(original, candidate);
    });
    if (iter == repaired.end()) {
        return std::nullopt;
    }
    return *iter;
}

std::vector<std::string> sorted_unique_strings(std::vector<std::string> values) {
    std::ranges::sort(values);
    values.erase(std::ranges::unique(values).begin(), values.end());
    return values;
}

std::vector<std::string> sample_evidence(const std::optional<Breakpoint>& survivor) {
    if (!survivor.has_value()) {
        return {};
    }
    return sorted_unique_strings(survivor->evidence_ids);
}

InterventionTrace replay_program(
    const Repository& source,
    std::string_view branch,
    const Breakpoint& breakpoint,
    const InterventionProgram& program,
    const Verifier& verifier,
    BreakpointFindOptions find_options,
    Hash256 search_id) {
    InterventionTrace trace;
    trace.search_id = search_id;
    trace.program = program;

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

    std::vector<DelayedRelation> delayed;
    try {
        for (std::size_t index = 1; index < history.size(); ++index) {
            const auto& record = history[index];
            if (record.origin == TransactionOrigin::Internal) {
                continue;
            }
            const auto stored = source.backend().stored_commit(record.id);
            const auto original = source.objects().get_canonical<Delta>(stored.delta_object);
            const auto before = repository.world(branch).snapshot();
            auto edited = edit_delta_for_program(before, original, program, record.id);
            trace.transformed = trace.transformed || edited.changed;
            delayed.insert(
                delayed.end(),
                std::make_move_iterator(edited.delayed.begin()),
                std::make_move_iterator(edited.delayed.end()));
            const auto delta_hash = canonical_hash(edited.delta);
            if (auto replayed = commit_delta(repository, branch, std::move(edited.delta), record.label, verifier);
                replayed.has_value()) {
                trace.law_statuses.insert(trace.law_statuses.end(), replayed->law_statuses.begin(), replayed->law_statuses.end());
                trace.violations.insert(trace.violations.end(), replayed->violations.begin(), replayed->violations.end());
                trace.steps.push_back(InterventionTraceStep{
                    record.id,
                    replayed->id,
                    delta_hash,
                    edited.changed,
                    record.label,
                    replayed->law_statuses,
                    replayed->violations
                });
            }
        }
        for (const auto& item : delayed) {
            if (auto replayed = append_delayed(repository, branch, item, verifier); replayed.has_value()) {
                trace.law_statuses.insert(trace.law_statuses.end(), replayed->law_statuses.begin(), replayed->law_statuses.end());
                trace.violations.insert(trace.violations.end(), replayed->violations.begin(), replayed->violations.end());
                trace.steps.push_back(InterventionTraceStep{
                    {},
                    replayed->id,
                    replayed->delta_hash,
                    true,
                    "intervention delay",
                    replayed->law_statuses,
                    replayed->violations
                });
            }
        }

        ProjectionStore store{repository};
        trace.breakpoints = BreakpointFinder{}.find(store, branch, find_options);
        trace.survivor = surviving_breakpoint_local(breakpoint, trace.breakpoints);
        trace.survives = trace.survivor.has_value();
        trace.evidence_ids = sample_evidence(trace.survivor);
        trace.replayed = true;
        trace.explanation = trace.survives
            ? "equivalent breakpoint survived intervention replay"
            : "equivalent breakpoint did not survive intervention replay";
        cleanup();
        return trace;
    } catch (...) {
        cleanup();
        throw;
    }
}

std::vector<std::string> carried_evidence(const std::vector<InterventionSearchSample>& samples) {
    std::vector<std::string> carried;
    bool initialized = false;
    for (const auto& sample : samples) {
        if (!sample.replayed || !sample.survives) {
            continue;
        }
        auto evidence = sorted_unique_strings(sample.evidence_ids);
        if (!initialized) {
            carried = std::move(evidence);
            initialized = true;
            continue;
        }
        std::vector<std::string> intersection;
        std::ranges::set_intersection(carried, evidence, std::back_inserter(intersection));
        carried = std::move(intersection);
    }
    return carried;
}

ScaleValue program_scale(const InterventionProgram& program) {
    ScaleValue scale = ScaleValue::zero();
    for (const auto& op : program.operators) {
        if (op.scale > scale) {
            scale = op.scale;
        }
    }
    return scale;
}

void summarize_search(InterventionSearchResult& result) {
    std::vector<InterventionSearchSample> ordered = result.samples;
    std::ranges::sort(ordered, [](const auto& left, const auto& right) {
        if (left.scale != right.scale) {
            return left.scale < right.scale;
        }
        return to_hex(left.program.canonical_hash) < to_hex(right.program.canonical_hash);
    });

    bool in_region = false;
    InterventionSurvivalRegion region;
    const auto max_scale = ordered.empty() ? ScaleValue::zero() : ordered.back().scale;
    for (const auto& sample : ordered) {
        if (!sample.survives) {
            if (!result.minimal_killing_scale.has_value()) {
                result.minimal_killing_scale = sample.scale;
            }
            if (in_region) {
                region.death_scale = sample.scale;
                region.persistence_length = region.death_scale.to_double() - region.birth_scale.to_double();
                result.persistence_length += region.persistence_length;
                if (!result.death_scale.has_value()) {
                    result.death_scale = region.death_scale;
                }
                result.surviving_regions.push_back(region);
                region = {};
                in_region = false;
            }
            continue;
        }

        if (!result.birth_scale.has_value()) {
            result.birth_scale = sample.scale;
        }
        if (!in_region) {
            region = {};
            region.birth_scale = sample.scale;
            in_region = true;
        }
        region.last_surviving_scale = sample.scale;
    }
    if (in_region) {
        region.death_scale = max_scale;
        region.survives_to_max_scale = true;
        region.persistence_length = region.death_scale.to_double() - region.birth_scale.to_double();
        result.persistence_length += region.persistence_length;
        result.surviving_regions.push_back(region);
    }
    result.carried_evidence_ids = carried_evidence(result.samples);
}

std::vector<InterventionProgram> build_programs(
    const std::vector<OperatorFamily>& families,
    InterventionSearchOptions options) {
    if (options.max_composition > 2) {
        throw std::invalid_argument("intervention search v1 supports --max-composition up to 2");
    }

    std::vector<InterventionOperator> operators;
    for (const auto& family : families) {
        auto refined = refine_family(family, options.max_depth);
        operators.insert(
            operators.end(),
            std::make_move_iterator(refined.begin()),
            std::make_move_iterator(refined.end()));
    }
    std::ranges::sort(operators, [](const auto& left, const auto& right) {
        if (left.canonical_cost != right.canonical_cost) {
            return left.canonical_cost < right.canonical_cost;
        }
        return to_hex(left.canonical_hash) < to_hex(right.canonical_hash);
    });

    std::vector<InterventionProgram> programs;
    if (options.include_identity) {
        programs.push_back(identity_intervention_program());
    }
    for (const auto& op : operators) {
        programs.push_back(make_intervention_program({op}));
    }
    if (options.max_composition >= 2) {
        for (std::size_t left = 0; left < operators.size(); ++left) {
            for (std::size_t right = 0; right < operators.size(); ++right) {
                if (left == right) {
                    continue;
                }
                programs.push_back(make_intervention_program({operators[left], operators[right]}));
            }
        }
    }

    std::ranges::sort(programs, [](const auto& left, const auto& right) {
        if (left.canonical_cost != right.canonical_cost) {
            return left.canonical_cost < right.canonical_cost;
        }
        return to_hex(left.canonical_hash) < to_hex(right.canonical_hash);
    });
    programs.erase(std::ranges::unique(programs, {}, &InterventionProgram::canonical_hash).begin(), programs.end());
    return programs;
}

std::string evidence_text(const std::vector<std::string>& evidence) {
    if (evidence.empty()) {
        return "none";
    }
    std::ostringstream output;
    for (std::size_t index = 0; index < evidence.size(); ++index) {
        if (index != 0) {
            output << ", ";
        }
        output << evidence[index];
    }
    return output.str();
}

std::string scale_text(const std::optional<ScaleValue>& scale) {
    if (!scale.has_value()) {
        return "none";
    }
    return to_string(*scale);
}

}  // namespace

Hash256 intervention_search_id(
    const Repository& repository,
    std::string_view branch,
    const Breakpoint& breakpoint,
    const std::vector<OperatorFamily>& families,
    InterventionSearchOptions options,
    BreakpointFindOptions find_options) {
    CanonicalWriter writer;
    writer.string("InterventionSearch:v1");
    writer.string(std::string{branch});
    const auto history = repository.backend().history(branch);
    writer.hash(history.empty() ? Hash256{} : history.back().id.value);
    writer.string(breakpoint.id);
    writer.u64(options.max_depth);
    writer.u64(options.max_composition);
    writer.u8(options.include_identity ? 1 : 0);
    writer.u64(find_options.repeated_relation_threshold);
    writer.u64(find_options.concentration_min_events);
    writer.f64(find_options.concentration_share);
    writer.u8(find_options.include_branch_divergence ? 1 : 0);
    writer.u64(families.size());
    for (const auto& family : families) {
        writer.hash(family.canonical_hash);
    }
    writer.string("verifier:active:v1");
    return sha256(writer.bytes());
}

InterventionSearchResult InterventionSearch::search(
    const Repository& repository,
    const ProjectionStore& store,
    std::string_view branch,
    const Breakpoint& breakpoint,
    const Verifier* verifier,
    BreakpointFindOptions find_options,
    InterventionSearchOptions options) const {
    Verifier default_verifier;
    const auto& replay_verifier = verifier == nullptr ? default_verifier : *verifier;
    auto families = OperatorFamilyBuilder{}.build(store, branch, breakpoint);
    auto programs = build_programs(families, options);

    InterventionSearchResult result;
    result.breakpoint = breakpoint;
    result.families = families;
    result.search_id = intervention_search_id(repository, branch, breakpoint, families, options, find_options);

    for (const auto& program : programs) {
        InterventionSearchSample sample;
        sample.scale = program_scale(program);
        sample.program = program;
        try {
            auto replayed = replay_program(
                repository,
                branch,
                breakpoint,
                program,
                replay_verifier,
                find_options,
                result.search_id);
            sample.replayed = replayed.replayed;
            sample.transformed = replayed.transformed;
            sample.survives = replayed.survives;
            sample.survivor = replayed.survivor;
            sample.evidence_ids = replayed.evidence_ids;
            sample.explanation = replayed.explanation;
            result.replayed_branches += replayed.replayed ? 1U : 0U;
        } catch (const std::exception& error) {
            sample.replayed = false;
            sample.transformed = false;
            sample.survives = true;
            sample.explanation = fmt::format("intervention replay failed: {}", error.what());
        }
        if (sample.transformed && !sample.survives) {
            if (!result.minimal_killing_program.has_value()
                || sample.program.canonical_cost < result.minimal_killing_program->canonical_cost
                || (sample.program.canonical_cost == result.minimal_killing_program->canonical_cost
                    && to_hex(sample.program.canonical_hash) < to_hex(result.minimal_killing_program->canonical_hash))) {
                result.minimal_killing_program = sample.program;
            }
        }
        result.samples.push_back(std::move(sample));
    }
    result.programs_tested = result.samples.size();
    summarize_search(result);
    return result;
}

InterventionTrace InterventionSearch::trace(
    const Repository& repository,
    const ProjectionStore& store,
    std::string_view branch,
    const Breakpoint& breakpoint,
    const InterventionProgram& program,
    const Verifier* verifier,
    BreakpointFindOptions find_options,
    InterventionSearchOptions options) const {
    Verifier default_verifier;
    const auto& replay_verifier = verifier == nullptr ? default_verifier : *verifier;
    const auto families = OperatorFamilyBuilder{}.build(store, branch, breakpoint);
    const auto id = intervention_search_id(repository, branch, breakpoint, families, options, find_options);
    return replay_program(repository, branch, breakpoint, program, replay_verifier, find_options, id);
}

InterventionCompositionResult InterventionSearch::compose_pair(
    const Repository& repository,
    const ProjectionStore& store,
    std::string_view branch,
    const Breakpoint& breakpoint,
    const InterventionProgram& left,
    const InterventionProgram& right,
    const Verifier* verifier,
    BreakpointFindOptions find_options,
    InterventionSearchOptions options) const {
    InterventionCompositionResult result;
    result.left = left;
    result.right = right;
    std::vector<InterventionOperator> operators = left.operators;
    operators.insert(operators.end(), right.operators.begin(), right.operators.end());
    result.composed = make_intervention_program(std::move(operators));
    result.order = compare_interventions(left, right);
    result.trace = trace(repository, store, branch, breakpoint, result.composed, verifier, find_options, options);
    result.compatible = result.trace.replayed && result.trace.violations.empty();
    result.conflicting = !result.compatible;
    result.redundant = left.canonical_hash == right.canonical_hash || result.order == InterventionOrder::Equivalent;

    std::vector<InterventionOperator> reversed = right.operators;
    reversed.insert(reversed.end(), left.operators.begin(), left.operators.end());
    const auto reversed_program = make_intervention_program(std::move(reversed));
    const auto reversed_trace = trace(repository, store, branch, breakpoint, reversed_program, verifier, find_options, options);
    result.order_sensitive = reversed_trace.survives != result.trace.survives
        || reversed_trace.transformed != result.trace.transformed
        || reversed_trace.violations.size() != result.trace.violations.size();
    result.explanation = result.compatible
        ? "pair replayed without law violations"
        : "pair replay produced law violations or failed compatibility";
    return result;
}

std::string render_intervention_families_text(
    const std::vector<OperatorFamily>& families) {
    std::ostringstream output;
    output << "Intervention families\n";
    output << "---------------------\n";
    if (families.empty()) {
        output << "none\n";
        return output.str();
    }
    for (const auto& family : families) {
        output << fmt::format(
            "{} {} hash={}\n",
            family.id,
            to_string(family.kind),
            to_hex(family.canonical_hash).substr(0, 12));
    }
    return output.str();
}

std::string render_intervention_refinement_text(
    const std::vector<OperatorFamily>& families,
    std::uint8_t depth) {
    std::ostringstream output;
    output << fmt::format("Intervention refinement: depth {}\n", depth);
    output << "------------------------------\n";
    for (const auto& family : families) {
        output << fmt::format("{} {}\n", family.id, to_string(family.kind));
        for (const auto& op : refine_family(family, depth)) {
            output << fmt::format(
                "  op_{} scale={} cost={}\n",
                op.id,
                to_string(op.scale),
                op.canonical_cost);
        }
    }
    return output.str();
}

std::string render_intervention_search_text(const InterventionSearchResult& result) {
    std::ostringstream output;
    output << fmt::format("Intervention search: {}\n", result.breakpoint.id);
    output << "--------------------------------\n";
    output << fmt::format("search id: {}\n", to_hex(result.search_id).substr(0, 12));
    output << fmt::format("families: {}\n", result.families.size());
    output << fmt::format("programs tested: {}\n", result.programs_tested);
    output << fmt::format("replayed branches: {}\n", result.replayed_branches);
    output << fmt::format(
        "birth={} death={} persistence={:.6f} kill={}\n",
        scale_text(result.birth_scale),
        scale_text(result.death_scale),
        result.persistence_length,
        scale_text(result.minimal_killing_scale));
    if (result.minimal_killing_program.has_value()) {
        output << "minimal killing program:\n";
        output << fmt::format("  id: {}\n", intervention_program_id(*result.minimal_killing_program));
        output << fmt::format("  cost: {}\n", result.minimal_killing_program->canonical_cost);
        for (const auto& op : result.minimal_killing_program->operators) {
            output << fmt::format("  op_{} {} scale={}\n", op.id, to_string(op.kind), to_string(op.scale));
        }
    } else {
        output << "minimal killing program: none\n";
    }
    output << "survival intervals:\n";
    for (const auto& region : result.surviving_regions) {
        output << fmt::format(
            "  survives on [{}, {}{} last={} persistence={:.6f}\n",
            to_string(region.birth_scale),
            to_string(region.death_scale),
            region.survives_to_max_scale ? "]" : ")",
            to_string(region.last_surviving_scale),
            region.persistence_length);
    }
    output << fmt::format("evidence carried across scales: {}\n", evidence_text(result.carried_evidence_ids));
    return output.str();
}

std::string render_intervention_lattice_text(const InterventionSearchResult& result) {
    std::ostringstream output;
    output << fmt::format("Intervention lattice: {}\n", result.breakpoint.id);
    output << "---------------------\n";
    std::size_t alternatives = 0;
    for (const auto& left : result.samples) {
        if (left.survives) {
            continue;
        }
        for (const auto& right : result.samples) {
            if (right.survives || left.program.canonical_hash == right.program.canonical_hash) {
                continue;
            }
            const auto order = compare_interventions(left.program, right.program);
            if (order == InterventionOrder::Incomparable) {
                alternatives += 1;
                output << fmt::format(
                    "  {} incomparable {}\n",
                    intervention_program_id(left.program),
                    intervention_program_id(right.program));
                if (alternatives >= 8) {
                    return output.str();
                }
            }
        }
    }
    if (alternatives == 0) {
        output << "none\n";
    }
    return output.str();
}

std::string render_intervention_composition_text(const InterventionCompositionResult& result) {
    std::ostringstream output;
    output << "Intervention composition\n";
    output << "------------------------\n";
    output << fmt::format("left:         {}\n", intervention_program_id(result.left));
    output << fmt::format("right:        {}\n", intervention_program_id(result.right));
    output << fmt::format("composed:     {}\n", intervention_program_id(result.composed));
    output << fmt::format("order:        {}\n", to_string(result.order));
    output << fmt::format("compatible:   {}\n", result.compatible ? "yes" : "no");
    output << fmt::format("conflicting:  {}\n", result.conflicting ? "yes" : "no");
    output << fmt::format("redundant:    {}\n", result.redundant ? "yes" : "no");
    output << fmt::format("sensitive:    {}\n", result.order_sensitive ? "yes" : "no");
    output << fmt::format("survives:     {}\n", result.trace.survives ? "yes" : "no");
    output << fmt::format("steps:        {}\n", result.trace.steps.size());
    output << fmt::format("explanation:  {}\n", result.explanation);
    return output.str();
}

}  // namespace pv
