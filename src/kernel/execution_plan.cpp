// SPDX-License-Identifier: Apache-2.0
#include "pv/kernel/execution_plan.hpp"

#include <algorithm>

#include "pv/hash/hasher.hpp"
#include "pv/kernel/merkle.hpp"
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
    writer.string("ExecutionPlan:v1");
    writer.u64(plan.tx.id.value);
    writer.u8(static_cast<std::uint8_t>(plan.tx.origin));
    writer.string(plan.tx.label);
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
    proof.before_root = before_root.root;
    proof.operation_root = canonical_hash(plan.tx.delta);
    proof.read_set_root = compute_fact_id_root(reads);
    proof.write_set_root = compute_fact_id_root(writes);
    proof.law_input_root = hash_hashes(
        "LawInputRoot:v1",
        {proof.before_root, proof.operation_root, proof.read_set_root});
    proof.law_output_root = hash_hashes(
        "LawOutputRoot:v1",
        {canonical_hash(plan.verification.statuses), canonical_hash(plan.verification.violations)});
    proof.after_root = after_root.root;
    return proof;
}

}  // namespace pv
