// SPDX-License-Identifier: Apache-2.0
#include "pv/runtime/transaction.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <map>
#include <stdexcept>

#include "pv/core/world_index.hpp"
#include "pv/kernel/executor.hpp"
#include "pv/kernel/fact_store.hpp"
#include "pv/kernel/merkle.hpp"
#include "pv/kernel/vm.hpp"

namespace pv {
namespace {

std::string overlay_reason(OverlayError error) {
    return fmt::format("overlay rejected transaction: {}", to_string(error));
}

void append_law_trace(World& world, std::vector<TraceEvent>& events, const VerificationResult& verification) {
    for (const auto& status : verification.statuses) {
        events.push_back(TraceEvent{
            world.epoch(),
            "law.check",
            {
                {"world", world.name()},
                {"law", status.law},
                {"status", status.passed ? "ok" : to_string(status.severity)},
                {"detail", status.explanation}
            },
            {{"magnitude", status.magnitude}}
        });
    }
}

std::uint64_t next_pointer_value(const WorldSnapshot& snapshot) noexcept {
    std::uint64_t next = 1;
    for (const auto& pointer : snapshot.pointers) {
        next = std::max(next, pointer.id.value + 1);
    }
    return next;
}

std::optional<ObjectId> object_named(const WorldSnapshot& snapshot, std::string_view name) {
    for (const auto& object : snapshot.objects) {
        if (object.name == name) {
            return object.id;
        }
    }
    return std::nullopt;
}

void add_object_facts(const FactStore& store, ObjectId object, std::vector<FactId>& out) {
    for (const auto& fact : store.facts()) {
        if (const auto* payload = std::get_if<ObjectFactPayload>(&fact.payload)) {
            if (payload->object == object) {
                out.push_back(fact.id);
            }
        } else if (const auto* payload = std::get_if<AttributeFactPayload>(&fact.payload)) {
            if (const auto* subject = std::get_if<ObjectAttributeSubject>(&payload->subject);
                subject != nullptr && subject->object == object) {
                out.push_back(fact.id);
            }
        }
    }
}

void add_pointer_facts(const FactStore& store, PointerId pointer, std::vector<FactId>& out) {
    for (const auto& fact : store.facts()) {
        if (const auto* payload = std::get_if<PointerFactPayload>(&fact.payload)) {
            if (payload->pointer == pointer) {
                out.push_back(fact.id);
            }
        } else if (const auto* payload = std::get_if<AttributeFactPayload>(&fact.payload)) {
            if (const auto* subject = std::get_if<PointerAttributeSubject>(&payload->subject);
                subject != nullptr && subject->pointer == pointer) {
                out.push_back(fact.id);
            }
        }
    }
}

void sort_unique_objects(std::vector<ObjectId>& objects) {
    std::ranges::sort(objects, [](ObjectId left, ObjectId right) {
        if (left.index != right.index) {
            return left.index < right.index;
        }
        return left.generation < right.generation;
    });
    objects.erase(std::ranges::unique(objects).begin(), objects.end());
}

void sort_unique_pointers(std::vector<PointerId>& pointers) {
    std::ranges::sort(pointers, [](PointerId left, PointerId right) {
        return left.value < right.value;
    });
    pointers.erase(std::ranges::unique(pointers).begin(), pointers.end());
}

[[maybe_unused]] std::vector<ResolvedOperation> resolve_operations(
    const Delta& delta,
    const WorldSnapshot& before,
    const WorldSnapshot& after,
    const FactStore& before_facts,
    const FactStore& after_facts) {
    std::vector<ResolvedOperation> resolved;
    resolved.reserve(delta.ops.size());

    std::map<std::uint32_t, ObjectId> temps;
    std::uint64_t next_pointer = next_pointer_value(before);

    auto resolve_before_or_after = [&](const ObjectRef& ref) -> std::optional<ObjectId> {
        if (const auto* id = std::get_if<ObjectId>(&ref)) {
            if (before.contains(*id) || after.contains(*id)) {
                return *id;
            }
            return std::nullopt;
        }
        const auto temp = std::get<TempObjectId>(ref);
        const auto iter = temps.find(temp.value);
        if (iter == temps.end()) {
            return std::nullopt;
        }
        return iter->second;
    };

    for (const auto& op : delta.ops) {
        ResolvedOperation item;
        item.id = op.id;
        item.kind = op.kind;

        switch (op.kind) {
        case OperationKind::InternType:
        case OperationKind::InternRelation:
        case OperationKind::AssertFact:
            break;
        case OperationKind::AssertObject: {
            if (const auto id = resolve_before_or_after(std::get<AssertObjectOp>(op.body).object); id.has_value()) {
                item.touched_objects.push_back(*id);
                add_object_facts(before_facts, *id, item.reads);
            }
            break;
        }
        case OperationKind::AssertPointer: {
            const auto pointer = std::get<AssertPointerOp>(op.body).id;
            item.touched_pointers.push_back(pointer);
            add_pointer_facts(before_facts, pointer, item.reads);
            break;
        }
        case OperationKind::CreateObject: {
            const auto& body = std::get<CreateObjectOp>(op.body);
            if (const auto id = object_named(after, body.name); id.has_value()) {
                temps.emplace(body.temp_id.value, *id);
                item.touched_objects.push_back(*id);
                add_object_facts(after_facts, *id, item.writes);
            }
            break;
        }
        case OperationKind::SetObjectType: {
            if (const auto id = resolve_before_or_after(std::get<SetObjectTypeOp>(op.body).object); id.has_value()) {
                item.touched_objects.push_back(*id);
                add_object_facts(before_facts, *id, item.reads);
                add_object_facts(after_facts, *id, item.writes);
            }
            break;
        }
        case OperationKind::SetObjectExistence: {
            if (const auto id = resolve_before_or_after(std::get<SetObjectExistenceOp>(op.body).object); id.has_value()) {
                item.touched_objects.push_back(*id);
                add_object_facts(before_facts, *id, item.reads);
                add_object_facts(after_facts, *id, item.writes);
            }
            break;
        }
        case OperationKind::SetObjectAttribute: {
            if (const auto id = resolve_before_or_after(std::get<SetObjectAttributeOp>(op.body).object); id.has_value()) {
                item.touched_objects.push_back(*id);
                add_object_facts(before_facts, *id, item.reads);
                add_object_facts(after_facts, *id, item.writes);
            }
            break;
        }
        case OperationKind::RemoveObjectAttribute: {
            if (const auto id = resolve_before_or_after(std::get<RemoveObjectAttributeOp>(op.body).object); id.has_value()) {
                item.touched_objects.push_back(*id);
                add_object_facts(before_facts, *id, item.reads);
                add_object_facts(after_facts, *id, item.writes);
            }
            break;
        }
        case OperationKind::CreatePointer: {
            const auto& body = std::get<CreatePointerOp>(op.body);
            const auto from = resolve_before_or_after(body.from);
            const auto to = resolve_before_or_after(body.to);
            if (from.has_value()) {
                item.touched_objects.push_back(*from);
                add_object_facts(before_facts, *from, item.reads);
            }
            if (to.has_value()) {
                item.touched_objects.push_back(*to);
                add_object_facts(before_facts, *to, item.reads);
            }
            const auto pointer = PointerId{next_pointer++};
            item.touched_pointers.push_back(pointer);
            add_pointer_facts(after_facts, pointer, item.writes);
            break;
        }
        case OperationKind::ExpirePointer: {
            const auto pointer = std::get<ExpirePointerOp>(op.body).id;
            item.touched_pointers.push_back(pointer);
            add_pointer_facts(before_facts, pointer, item.reads);
            add_pointer_facts(after_facts, pointer, item.writes);
            break;
        }
        case OperationKind::SetPointerWeight: {
            const auto pointer = std::get<SetPointerWeightOp>(op.body).id;
            item.touched_pointers.push_back(pointer);
            add_pointer_facts(before_facts, pointer, item.reads);
            add_pointer_facts(after_facts, pointer, item.writes);
            break;
        }
        case OperationKind::SetPointerAttribute: {
            const auto pointer = std::get<SetPointerAttributeOp>(op.body).id;
            item.touched_pointers.push_back(pointer);
            add_pointer_facts(before_facts, pointer, item.reads);
            add_pointer_facts(after_facts, pointer, item.writes);
            break;
        }
        case OperationKind::RemovePointerAttribute: {
            const auto pointer = std::get<RemovePointerAttributeOp>(op.body).id;
            item.touched_pointers.push_back(pointer);
            add_pointer_facts(before_facts, pointer, item.reads);
            add_pointer_facts(after_facts, pointer, item.writes);
            break;
        }
        case OperationKind::EmitEvent:
            break;
        }

        sort_unique_objects(item.touched_objects);
        sort_unique_pointers(item.touched_pointers);
        item.reads = unique_fact_ids(std::move(item.reads));
        item.writes = unique_fact_ids(std::move(item.writes));
        resolved.push_back(std::move(item));
    }
    return resolved;
}

}  // namespace

PreparedTransaction prepare_transaction(const World& world, const Transaction& tx, const Verifier& verifier) {
    PreparedTransaction prepared;
    prepared.tx = tx;
    prepared.before = world.snapshot();

    if (tx.program.has_value()) {
        const auto vm = KernelVm{}.execute(prepared.before, *tx.program);
        if (!vm.ok) {
            prepared.rejection_reason = format_vm_diagnostics(vm.diagnostics);
            return prepared;
        }
        if (canonical_hash(vm.delta) != canonical_hash(tx.delta)) {
            prepared.rejection_reason = "transaction delta does not match VM(program).delta";
            return prepared;
        }
    }

    prepared.predicted_after = SnapshotOverlay{prepared.before}.apply(tx.delta);

    if (tx.delta.empty() && !tx.allow_empty) {
        prepared.rejection_reason = "empty transaction rejected";
        return prepared;
    }

    if (tx.expected_base_epoch.has_value() && *tx.expected_base_epoch != prepared.before.epoch) {
        prepared.rejection_reason = fmt::format(
            "transaction expected base epoch {}, got {}",
            tx.expected_base_epoch->value,
            prepared.before.epoch.value);
        return prepared;
    }

    if (tx.input_snapshot_hash.has_value() && *tx.input_snapshot_hash != prepared.before.canonical_hash()) {
        prepared.rejection_reason = "transaction input snapshot hash does not match current world";
        return prepared;
    }

    if (!prepared.predicted_after.has_value()) {
        prepared.rejection_reason = overlay_reason(prepared.predicted_after.error());
        return prepared;
    }

    WorldIndex before_index;
    before_index.rebuild(prepared.before);
    WorldIndex after_index;
    after_index.rebuild(*prepared.predicted_after);
    const auto before_facts = FactStore::from_snapshot(prepared.before);
    const auto after_facts = FactStore::from_snapshot(*prepared.predicted_after);

    ExecutionPlan plan = make_execution_plan(tx, prepared.before, *prepared.predicted_after, VerificationResult{});

    prepared.verification = verifier.check(LawCheckContext{
        prepared.before,
        tx.delta,
        *prepared.predicted_after,
        &plan,
        &before_index,
        &after_index,
        &before_facts,
        &after_facts
    });
    plan.verification = prepared.verification;
    auto sealed = seal_execution_plan(std::move(plan));
    prepared.execution_plan = std::move(sealed.plan);
    prepared.proof = sealed.proof;
    if (!prepared.verification.accepted) {
        prepared.rejection_reason = "transition rejected by active laws";
        return prepared;
    }

    try {
        prepared.predicted_events = world.preview_delta_unchecked(tx.delta);
    } catch (const std::exception& error) {
        prepared.rejection_reason = error.what();
        return prepared;
    }

    prepared.committable = true;
    return prepared;
}

CommitResult commit_prepared(World& world, const PreparedTransaction& prepared) {
    CommitResult result;
    result.before_epoch = world.epoch();
    result.law_statuses = prepared.verification.statuses;
    result.violations = prepared.verification.violations;

    const auto current_hash = world.canonical_hash();
    const auto prepared_hash = prepared.before.canonical_hash();
    if (world.epoch() != prepared.before.epoch || current_hash != prepared_hash) {
        result.events = world.append_rejection_trace(
            prepared.tx.delta,
            "prepared transaction does not match current world",
            {});
        result.after_epoch = world.epoch();
        result.accepted = false;
        result.world_hash = world.hash();
        return result;
    }

    if (!prepared.committable) {
        result.events = world.append_rejection_trace(
            prepared.tx.delta,
            prepared.rejection_reason.empty() ? "transaction rejected" : prepared.rejection_reason,
            prepared.verification.violations);
        result.after_epoch = world.epoch();
        result.accepted = false;
        result.world_hash = world.hash();
        return result;
    }

    auto events = world.apply_delta_unchecked(prepared.tx.delta);
    append_law_trace(world, events, prepared.verification);
    world.trace_.append(events);

    result.accepted = true;
    result.after_epoch = world.epoch();
    result.events = std::move(events);
    result.world_hash = world.hash();
    result.execution_plan_hash = prepared.execution_plan.plan_hash;
    result.proof = prepared.proof;
    if (prepared.proof.has_value()) {
        result.read_set_hash = prepared.proof->read_set_root;
        result.write_set_hash = prepared.proof->write_set_root;
        result.proof_hash = hash_commit_proof(*prepared.proof);
    }
    return result;
}

std::string to_string(TransactionOrigin origin) {
    switch (origin) {
    case TransactionOrigin::Manual:
        return "Manual";
    case TransactionOrigin::Script:
        return "Script";
    case TransactionOrigin::Morphism:
        return "Morphism";
    case TransactionOrigin::Evolution:
        return "Evolution";
    case TransactionOrigin::Replay:
        return "Replay";
    case TransactionOrigin::ForkMerge:
        return "ForkMerge";
    case TransactionOrigin::Internal:
        return "Internal";
    case TransactionOrigin::Ingestion:
        return "Ingestion";
    }
    return "Manual";
}

}  // namespace pv
