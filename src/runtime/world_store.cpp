// SPDX-License-Identifier: Apache-2.0
#include "pv/runtime/world_store.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <set>
#include <stdexcept>

#include "pv/core/snapshot_page.hpp"
#include "pv/kernel/program.hpp"
#include "pv/kernel/merkle.hpp"

namespace pv {
namespace {

std::vector<CommitId> parent_vector(const std::optional<CommitId>& parent) {
    if (!parent.has_value()) {
        return {};
    }
    return {*parent};
}

CommitRecord make_genesis_record(
    BranchId branch,
    const std::string& branch_name,
    const World& world,
    const WorldSnapshot& snapshot_value,
    SnapshotId snapshot,
    Hash256 hash) {
    CommitRecord record;
    record.world = world.id();
    record.branch = branch;
    record.branch_name = branch_name;
    record.before_epoch = world.epoch();
    record.after_epoch = world.epoch();
    record.before_snapshot = snapshot;
    record.after_snapshot = snapshot;
    record.before_hash = hash;
    record.after_hash = hash;
    record.delta_hash = canonical_hash(Delta{});
    record.trace_hash = canonical_hash(std::vector<TraceEvent>{});
    record.law_hash = canonical_hash(std::vector<LawStatus>{});
    record.violation_hash = canonical_hash(std::vector<LawViolation>{});
    record.morphism_path_hash = canonical_hash_morphism_path({});
    const auto root = compute_world_root(snapshot_value).root;
    const auto chunked = build_chunked_snapshot_plan(snapshot_value);
    record.before_root = root;
    record.after_root = root;
    record.checkpoint_snapshot_object = chunked.root_object;
    record.checkpoint_distance = 0;
    record.graph_page_roots = chunked.graph_page_roots();
    record.accepted = true;
    record.origin = TransactionOrigin::Internal;
    record.label = "genesis";
    record.id = make_commit_id(record);
    return record;
}

bool should_checkpoint(std::uint64_t distance, bool forced, const CheckpointPolicy& policy) {
    if (forced) {
        return true;
    }
    if (policy.every_n_commits > 0 && distance >= policy.every_n_commits) {
        return true;
    }
    return policy.max_delta_chain > 0 && distance >= policy.max_delta_chain;
}

Delta with_required_symbol_interns(const WorldSnapshot& before, const Delta& delta) {
    std::set<std::uint32_t> referenced_types;
    std::set<std::uint32_t> referenced_relations;
    std::set<std::uint32_t> interned_types;
    std::set<std::uint32_t> interned_relations;

    for (const auto& op : delta.ops) {
        switch (op.kind) {
        case OperationKind::InternType:
            interned_types.insert(std::get<InternTypeOp>(op.body).id.value);
            break;
        case OperationKind::InternRelation:
            interned_relations.insert(std::get<InternRelationOp>(op.body).id.id);
            break;
        case OperationKind::CreateObject:
            referenced_types.insert(std::get<CreateObjectOp>(op.body).type.value);
            break;
        case OperationKind::SetObjectType:
            referenced_types.insert(std::get<SetObjectTypeOp>(op.body).type.value);
            break;
        case OperationKind::CreatePointer:
            referenced_relations.insert(std::get<CreatePointerOp>(op.body).relation.id);
            break;
        default:
            break;
        }
    }

    Delta out;
    for (const auto id : referenced_types) {
        if (id == 0 || interned_types.contains(id)) {
            continue;
        }
        if (const auto iter = before.type_names.find(id); iter != before.type_names.end()) {
            out.append_intern_type(iter->second, TypeId{id});
        }
    }
    for (const auto id : referenced_relations) {
        if (id == 0 || interned_relations.contains(id)) {
            continue;
        }
        if (const auto iter = before.relation_names.find(id); iter != before.relation_names.end()) {
            out.append_intern_relation(iter->second, RelationType{id});
        }
    }
    for (const auto& op : delta.ops) {
        out.append(make_operation(op.kind, op.body));
    }
    return out;
}

TraceEvent make_fork_event(const Branch& source, const Branch& forked, const ForkResult& result) {
    return TraceEvent{
        source.epoch,
        "branch.fork",
        {
            {"from", source.name},
            {"to", forked.name},
            {"base_commit", to_string(result.base_commit)},
            {"base_hash", to_hex(result.base_hash)}
        },
        {}
    };
}

}  // namespace

WorldStore::WorldStore(CheckpointPolicy checkpoint_policy)
    : checkpoint_policy_(checkpoint_policy) {}

void WorldStore::set_checkpoint_policy(CheckpointPolicy policy) noexcept {
    checkpoint_policy_ = policy;
}

BranchId WorldStore::create_branch(std::string name, World initial) {
    if (name.empty()) {
        throw std::invalid_argument("branch name cannot be empty");
    }
    if (find_branch(name).has_value()) {
        throw std::invalid_argument(fmt::format("branch '{}' already exists", name));
    }

    const auto id = BranchId{next_branch_id_++};
    const auto snapshot_value = initial.snapshot();
    const auto snapshot = snapshots_.put(snapshot_value);
    const auto hash = initial.canonical_hash();
    auto genesis = make_genesis_record(id, name, initial, snapshot_value, snapshot, hash);
    snapshots_.bind_commit(genesis.id, snapshot);
    graph_.add_node(CommitNode{genesis.id, {}, {}, id, snapshot});

    Branch branch;
    branch.id = id;
    branch.name = std::move(name);
    branch.world = initial.id();
    branch.epoch = initial.epoch();
    branch.head = genesis.id;
    branch.head_snapshot = snapshot;

    BranchState state;
    state.branch = std::move(branch);
    state.world = std::move(initial);
    state.history.push_back(std::move(genesis));
    branches_.push_back(std::move(state));
    return id;
}

BranchId WorldStore::restore_branch(
    BranchId id,
    std::string name,
    World head_world,
    std::vector<CommitRecord> history,
    std::vector<std::pair<CommitId, WorldSnapshot>> snapshot_values) {
    if (!id.valid()) {
        throw std::invalid_argument("branch id must be valid");
    }
    if (name.empty()) {
        throw std::invalid_argument("branch name cannot be empty");
    }
    if (find_branch(name).has_value()) {
        throw std::invalid_argument(fmt::format("branch '{}' already exists", name));
    }

    std::vector<std::pair<Hash256, SnapshotId>> snapshot_ids;
    snapshot_ids.reserve(snapshot_values.size());
    for (auto& [commit, snapshot] : snapshot_values) {
        const auto hash = snapshot.canonical_hash();
        const auto id_for_snapshot = snapshots_.put(std::move(snapshot));
        snapshots_.bind_commit(commit, id_for_snapshot);
        snapshot_ids.push_back({hash, id_for_snapshot});
    }

    auto snapshot_for_hash = [&](Hash256 hash) -> SnapshotId {
        for (const auto& [candidate_hash, snapshot] : snapshot_ids) {
            if (candidate_hash == hash) {
                return snapshot;
            }
        }
        return SnapshotId{};
    };

    std::optional<CommitId> head;
    SnapshotId head_snapshot;
    Epoch branch_epoch = head_world.epoch();
    for (auto& record : history) {
        if (const auto before = snapshot_for_hash(record.before_hash); before.valid()) {
            record.before_snapshot = before;
        }
        if (const auto after = snapshot_for_hash(record.after_hash); after.valid()) {
            record.after_snapshot = after;
        }
        if (record.accepted) {
            graph_.add_node(CommitNode{record.id, record.parents, {}, id, record.after_snapshot});
            head = record.id;
            head_snapshot = record.after_snapshot;
            branch_epoch = record.after_epoch;
        }
        next_transaction_id_ = std::max(next_transaction_id_, record.transaction.value + 1);
    }

    Branch branch;
    branch.id = id;
    branch.name = std::move(name);
    branch.world = head_world.id();
    branch.epoch = branch_epoch;
    branch.head = head;
    branch.head_snapshot = head_snapshot;

    BranchState state;
    state.branch = std::move(branch);
    state.world = std::move(head_world);
    state.history = std::move(history);
    branches_.push_back(std::move(state));
    next_branch_id_ = std::max(next_branch_id_, id.value + 1);
    return id;
}

BranchId WorldStore::fork_branch(BranchId source, std::string new_name) {
    return fork(source, std::move(new_name)).forked;
}

ForkResult WorldStore::fork(BranchId source, std::string new_name) {
    return fork_with_id(source, BranchId{next_branch_id_++}, std::move(new_name));
}

ForkResult WorldStore::fork_with_id(BranchId source, BranchId forked_id, std::string new_name) {
    const auto& source_state = state(source);
    if (!forked_id.valid()) {
        throw std::invalid_argument("forked branch id must be valid");
    }
    if (!source_state.branch.head.has_value()) {
        throw std::runtime_error("cannot fork branch without a head commit");
    }
    if (find_branch(new_name).has_value()) {
        throw std::invalid_argument(fmt::format("branch '{}' already exists", new_name));
    }

    Branch forked;
    forked.id = forked_id;
    forked.name = std::move(new_name);
    forked.world = source_state.branch.world;
    forked.epoch = source_state.branch.epoch;
    forked.head = source_state.branch.head;
    forked.head_snapshot = source_state.branch.head_snapshot;

    ForkResult result;
    result.source = source;
    result.forked = forked_id;
    result.base_commit = *source_state.branch.head;
    result.base_snapshot = source_state.branch.head_snapshot;
    result.base_hash = snapshots_.get(result.base_snapshot).canonical_hash();
    result.events.push_back(make_fork_event(source_state.branch, forked, result));

    BranchState fork_state;
    fork_state.branch = forked;
    fork_state.world = source_state.world;
    fork_state.world.trace_.append(result.events);
    fork_state.history = source_state.history;
    fork_state.checkpoint_on_next_commit = checkpoint_policy_.force_checkpoint_on_branch_fork;
    branches_.push_back(std::move(fork_state));
    next_branch_id_ = std::max(next_branch_id_, forked_id.value + 1);
    return result;
}

const World& WorldStore::world(BranchId branch) const {
    return state(branch).world;
}

World& WorldStore::mutable_world(BranchId branch) {
    return state(branch).world;
}

const Branch& WorldStore::branch(BranchId branch) const {
    return state(branch).branch;
}

std::optional<BranchId> WorldStore::find_branch(std::string_view name) const {
    for (const auto& candidate : branches_) {
        if (candidate.branch.name == name) {
            return candidate.branch.id;
        }
    }
    return std::nullopt;
}

Delta WorldStore::normalize_delta(BranchId branch, const Delta& delta) const {
    return with_required_symbol_interns(state(branch).world.snapshot(), delta);
}

std::optional<CommitRecord> WorldStore::commit(BranchId branch_id, Transaction tx, const Verifier& verifier) {
    auto& branch_state = state(branch_id);
    if (!tx.id.valid()) {
        tx.id = TransactionId{next_transaction_id_++};
    }
    const auto before_snapshot = branch_state.branch.head_snapshot;
    const auto before_hash = branch_state.world.canonical_hash();
    const auto parent = branch_state.branch.head;
    const auto* parent_record = parent.has_value() ? commit_record(*parent) : nullptr;
    auto prepared = prepare_transaction(branch_state.world, tx, verifier);
    const auto result = commit_prepared(branch_state.world, prepared);

    SnapshotId after_snapshot = before_snapshot;
    auto after_snapshot_value = prepared.before;
    if (result.accepted) {
        after_snapshot_value = branch_state.world.snapshot();
        after_snapshot = snapshots_.put(after_snapshot_value);
    }
    const auto after_hash = branch_state.world.canonical_hash();

    CommitRecord record;
    record.parent = parent;
    record.parents = parent_vector(parent);
    record.world = branch_state.world.id();
    record.branch = branch_state.branch.id;
    record.branch_name = branch_state.branch.name;
    record.transaction = tx.id;
    record.before_epoch = prepared.before.epoch;
    record.after_epoch = branch_state.world.epoch();
    record.before_snapshot = before_snapshot;
    record.after_snapshot = after_snapshot;
    record.before_hash = before_hash;
    record.after_hash = after_hash;
    record.delta_hash = canonical_hash(tx.delta);
    if (tx.program.has_value()) {
        record.program_hash = program_hash(*tx.program);
        record.instruction_root = instruction_root(*tx.program);
        record.symbol_table_hash = symbol_table_hash(tx.program->symbols);
    }
    record.trace_hash = canonical_hash(result.events);
    record.law_hash = canonical_hash(result.law_statuses);
    record.violation_hash = canonical_hash(result.violations);
    record.morphism_path_hash = canonical_hash_morphism_path(tx.morphism_path);
    record.execution_plan_hash = result.execution_plan_hash;
    record.read_set_hash = result.read_set_hash;
    record.write_set_hash = result.write_set_hash;
    record.proof_hash = result.proof_hash;
    record.proof = result.proof;
    record.before_root = compute_world_root(prepared.before).root;
    record.after_root = compute_world_root(after_snapshot_value).root;
    auto checkpoint_distance = parent_record == nullptr ? std::uint64_t{0} : parent_record->checkpoint_distance + 1;
    const auto checkpoint_now = result.accepted
        && should_checkpoint(checkpoint_distance, branch_state.checkpoint_on_next_commit || !parent.has_value(), checkpoint_policy_);
    if (!result.accepted && parent_record != nullptr) {
        checkpoint_distance = parent_record->checkpoint_distance;
    }
    if (checkpoint_now) {
        const auto chunked = build_chunked_snapshot_plan(after_snapshot_value);
        record.checkpoint_snapshot_object = chunked.root_object;
        record.checkpoint_distance = 0;
        record.graph_page_roots = chunked.graph_page_roots();
    } else {
        record.checkpoint_distance = checkpoint_distance;
    }
    record.accepted = result.accepted;
    record.origin = tx.origin;
    record.label = tx.label;
    record.law_statuses = result.law_statuses;
    record.violations = result.violations;
    record.events = result.events;
    record.id = make_commit_id(record);

    if (record.accepted) {
        snapshots_.bind_commit(record.id, after_snapshot);
        graph_.add_node(CommitNode{record.id, record.parents, {}, branch_state.branch.id, after_snapshot});
        branch_state.branch.head = record.id;
        branch_state.branch.head_snapshot = after_snapshot;
        branch_state.branch.epoch = branch_state.world.epoch();
        branch_state.checkpoint_on_next_commit = false;
    }

    branch_state.history.push_back(record);
    return record;
}

std::vector<CommitRecord> WorldStore::history(BranchId branch) const {
    return state(branch).history;
}

const CommitRecord* WorldStore::commit_record(CommitId id) const noexcept {
    for (const auto& branch : branches_) {
        for (const auto& record : branch.history) {
            if (record.id == id) {
                return &record;
            }
        }
    }
    return nullptr;
}

MergeAnalysis WorldStore::analyze_merge(BranchId left, BranchId right) const {
    return pv::analyze_merge(*this, left, right);
}

std::size_t WorldStore::branch_count() const noexcept {
    return branches_.size();
}

const CommitGraph& WorldStore::graph() const noexcept {
    return graph_;
}

const SnapshotStore& WorldStore::snapshots() const noexcept {
    return snapshots_;
}

WorldStore::BranchState& WorldStore::state(BranchId branch) {
    for (auto& candidate : branches_) {
        if (candidate.branch.id == branch) {
            return candidate;
        }
    }
    throw std::out_of_range(fmt::format("unknown branch {}", to_string(branch)));
}

const WorldStore::BranchState& WorldStore::state(BranchId branch) const {
    for (const auto& candidate : branches_) {
        if (candidate.branch.id == branch) {
            return candidate;
        }
    }
    throw std::out_of_range(fmt::format("unknown branch {}", to_string(branch)));
}

}  // namespace pv
