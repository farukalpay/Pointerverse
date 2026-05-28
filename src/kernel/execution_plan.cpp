// SPDX-License-Identifier: Apache-2.0
#include "pv/kernel/execution_plan.hpp"

#include <algorithm>
#include <map>

#include "pv/hash/hasher.hpp"
#include "pv/kernel/fact_store.hpp"
#include "pv/kernel/merkle.hpp"
#include "pv/kernel/program.hpp"
#include "pv/storage/canonical_codec.hpp"

namespace pv {
namespace {

void encode_object_id(CanonicalWriter& writer, ObjectId id) {
    writer.u32(id.index);
    writer.u32(id.generation);
}

void encode_pointer_id(CanonicalWriter& writer, PointerId id) {
    writer.u64(id.value);
}

Hash256 hash_writer(const CanonicalWriter& writer) {
    return sha256(writer.bytes());
}

std::vector<FactId> collect_reads(const ExecutionPlan& plan) {
    std::vector<FactId> out;
    for (const auto& op : plan.resolved_ops) {
        out.insert(out.end(), op.reads.begin(), op.reads.end());
    }
    return unique_fact_ids(std::move(out));
}

std::vector<FactId> collect_writes(const ExecutionPlan& plan) {
    std::vector<FactId> out;
    for (const auto& op : plan.resolved_ops) {
        out.insert(out.end(), op.writes.begin(), op.writes.end());
    }
    return unique_fact_ids(std::move(out));
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

std::vector<ResolvedOperation> resolve_operations(
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

Hash256 hash_hashes(std::string_view label, std::initializer_list<Hash256> hashes) {
    CanonicalWriter writer;
    writer.string(label);
    writer.u64(hashes.size());
    for (const auto& hash : hashes) {
        writer.hash(hash);
    }
    return hash_writer(writer);
}

}  // namespace

std::vector<FactId> unique_fact_ids(std::vector<FactId> ids) {
    std::ranges::sort(ids, [](FactId left, FactId right) {
        return left < right;
    });
    ids.erase(std::ranges::unique(ids).begin(), ids.end());
    return ids;
}

Hash256 hash_execution_plan(const ExecutionPlan& plan) {
    const auto before_root = compute_world_root(plan.before);
    const auto after_root = compute_world_root(plan.predicted_after);
    CanonicalWriter writer;
    writer.string("ExecutionPlan:v2");
    writer.u8(static_cast<std::uint8_t>(plan.tx.origin));
    writer.hash(plan.tx.program.has_value() ? program_hash(*plan.tx.program) : Hash256{});
    writer.hash(canonical_hash(plan.tx.delta));
    writer.hash(before_root.root);
    writer.hash(after_root.root);
    writer.u64(plan.resolved_ops.size());
    for (const auto& op : plan.resolved_ops) {
        writer.u64(op.id.value);
        writer.u8(static_cast<std::uint8_t>(op.kind));
        writer.u64(op.touched_objects.size());
        for (const auto& object : op.touched_objects) {
            encode_object_id(writer, object);
        }
        writer.u64(op.touched_pointers.size());
        for (const auto& pointer : op.touched_pointers) {
            encode_pointer_id(writer, pointer);
        }
        auto reads = unique_fact_ids(op.reads);
        writer.u64(reads.size());
        for (const auto& fact : reads) {
            writer.hash(fact.value);
        }
        auto writes = unique_fact_ids(op.writes);
        writer.u64(writes.size());
        for (const auto& fact : writes) {
            writer.hash(fact.value);
        }
    }
    writer.hash(canonical_hash(plan.verification.statuses));
    writer.hash(canonical_hash(plan.verification.violations));
    return hash_writer(writer);
}

CommitProof make_commit_proof(const ExecutionPlan& plan) {
    const auto before_root = compute_world_root(plan.before);
    const auto after_root = compute_world_root(plan.predicted_after);
    const auto reads = collect_reads(plan);
    const auto writes = collect_writes(plan);

    CommitProof proof;
    proof.program_root = plan.tx.program.has_value() ? program_hash(*plan.tx.program) : Hash256{};
    proof.before_root = before_root.root;
    proof.operation_root = canonical_hash(plan.tx.delta);
    proof.read_set_root = compute_fact_id_root(reads);
    proof.write_set_root = compute_fact_id_root(writes);
    proof.law_input_root = hash_hashes(
        "LawInputRoot:v1",
        {proof.program_root, proof.before_root, proof.operation_root, proof.read_set_root});
    proof.law_output_root = hash_hashes(
        "LawOutputRoot:v1",
        {canonical_hash(plan.verification.statuses), canonical_hash(plan.verification.violations)});
    proof.after_root = after_root.root;
    return proof;
}

ExecutionPlan make_execution_plan(
    Transaction tx,
    WorldSnapshot before,
    WorldSnapshot predicted_after,
    VerificationResult verification) {
    const auto before_facts = FactStore::from_snapshot(before);
    const auto after_facts = FactStore::from_snapshot(predicted_after);

    ExecutionPlan plan;
    plan.tx = std::move(tx);
    plan.before = std::move(before);
    plan.predicted_after = std::move(predicted_after);
    plan.verification = std::move(verification);
    plan.resolved_ops = resolve_operations(
        plan.tx.delta,
        plan.before,
        plan.predicted_after,
        before_facts,
        after_facts);
    return plan;
}

}  // namespace pv
