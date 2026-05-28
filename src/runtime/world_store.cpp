// SPDX-License-Identifier: Apache-2.0
#include "pv/runtime/world_store.hpp"

#include <fmt/format.h>

#include <stdexcept>

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
    record.accepted = true;
    record.origin = TransactionOrigin::Internal;
    record.label = "genesis";
    record.id = make_commit_id(record);
    return record;
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

BranchId WorldStore::create_branch(std::string name, World initial) {
    if (name.empty()) {
        throw std::invalid_argument("branch name cannot be empty");
    }
    if (find_branch(name).has_value()) {
        throw std::invalid_argument(fmt::format("branch '{}' already exists", name));
    }

    const auto id = BranchId{next_branch_id_++};
    const auto snapshot = snapshots_.put(initial.snapshot());
    const auto hash = initial.canonical_hash();
    auto genesis = make_genesis_record(id, name, initial, snapshot, hash);
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

BranchId WorldStore::fork_branch(BranchId source, std::string new_name) {
    return fork(source, std::move(new_name)).forked;
}

ForkResult WorldStore::fork(BranchId source, std::string new_name) {
    const auto& source_state = state(source);
    if (!source_state.branch.head.has_value()) {
        throw std::runtime_error("cannot fork branch without a head commit");
    }
    if (find_branch(new_name).has_value()) {
        throw std::invalid_argument(fmt::format("branch '{}' already exists", new_name));
    }

    const auto forked_id = BranchId{next_branch_id_++};
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
    branches_.push_back(std::move(fork_state));
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

std::optional<CommitRecord> WorldStore::commit(BranchId branch_id, Transaction tx, const Verifier& verifier) {
    auto& branch_state = state(branch_id);
    if (!tx.id.valid()) {
        tx.id = TransactionId{next_transaction_id_++};
    }

    const auto before_snapshot = branch_state.branch.head_snapshot;
    const auto before_hash = branch_state.world.canonical_hash();
    const auto parent = branch_state.branch.head;
    auto prepared = prepare_transaction(branch_state.world, tx, verifier);
    const auto result = commit_prepared(branch_state.world, prepared);

    SnapshotId after_snapshot = before_snapshot;
    if (result.accepted) {
        after_snapshot = snapshots_.put(branch_state.world.snapshot());
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
    record.trace_hash = canonical_hash(result.events);
    record.law_hash = canonical_hash(result.law_statuses);
    record.violation_hash = canonical_hash(result.violations);
    record.morphism_path_hash = canonical_hash_morphism_path(tx.morphism_path);
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
