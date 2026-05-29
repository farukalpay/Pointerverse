// SPDX-License-Identifier: Apache-2.0
#include "pv/measure/repair_measure.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "pv/hash/hasher.hpp"
#include "pv/kernel/canonical_codec.hpp"
#include "pv/storage/object_codec.hpp"
#include "pv/storage/repository.hpp"

namespace pv {
namespace {

bool active_at(const PointerSnapshot& pointer, Epoch epoch) noexcept {
    return pointer.born_at <= epoch && (!pointer.expires_at.has_value() || epoch < *pointer.expires_at);
}

void write_object(CanonicalWriter& writer, ObjectId id) {
    writer.u32(id.index);
    writer.u32(id.generation);
}

void write_pointer(CanonicalWriter& writer, PointerId id) {
    writer.u64(id.value);
}

void sort_objects(std::vector<ObjectId>& objects) {
    std::ranges::sort(objects, [](ObjectId left, ObjectId right) {
        return left < right;
    });
    objects.erase(std::ranges::unique(objects).begin(), objects.end());
}

void sort_pointers(std::vector<PointerId>& pointers) {
    std::ranges::sort(pointers, {}, &PointerId::value);
    pointers.erase(std::ranges::unique(pointers).begin(), pointers.end());
}

std::vector<ObjectId> violation_objects(const CommitRecord& record) {
    std::vector<ObjectId> objects;
    for (const auto& violation : record.violations) {
        objects.insert(objects.end(), violation.objects.begin(), violation.objects.end());
    }
    sort_objects(objects);
    return objects;
}

std::vector<PointerId> violation_pointers(const CommitRecord& record) {
    std::vector<PointerId> pointers;
    for (const auto& violation : record.violations) {
        pointers.insert(pointers.end(), violation.pointers.begin(), violation.pointers.end());
    }
    sort_pointers(pointers);
    return pointers;
}

std::vector<LawId> violation_laws(const CommitRecord& record) {
    std::vector<LawId> laws;
    for (const auto& violation : record.violations) {
        laws.push_back(violation.law);
    }
    std::ranges::sort(laws);
    laws.erase(std::ranges::unique(laws).begin(), laws.end());
    return laws;
}

WorldSnapshot before_snapshot_for(const Repository& repository, const CommitRecord& record, const WorldSnapshot& after) {
    if (record.parent.has_value()) {
        return repository.backend().snapshot(*record.parent);
    }
    WorldSnapshot before;
    before.world = record.world;
    before.world_name = after.world_name;
    before.epoch = record.before_epoch;
    return before;
}

struct RepairProblem {
    CommitRecord record;
    WorldSnapshot before;
    WorldSnapshot target;
};

RepairProblem repair_problem_for(const Repository& repository, CommitId commit) {
    auto record = repository.backend().commit_record(commit);
    const auto stored = repository.backend().stored_commit(commit);
    const auto delta = repository.objects().get_canonical<Delta>(stored.delta_object);
    auto target = repository.backend().snapshot(commit);
    const auto before = before_snapshot_for(repository, record, target);
    if (!record.accepted) {
        target = before;
        if (const auto predicted = apply_delta_to_snapshot(before, delta); predicted.has_value()) {
            target = *predicted;
        }
    }
    return RepairProblem{std::move(record), before, std::move(target)};
}

bool legal_state(const Verifier& verifier, const WorldSnapshot& before, const WorldSnapshot& after) {
    const auto result = verifier.check(LawCheckContext{before, Delta{}, after});
    return result.violations.empty();
}

const Attribute* attribute_named(const std::vector<Attribute>& attributes, std::string_view name) {
    for (const auto& attribute : attributes) {
        if (attribute.key == name) {
            return &attribute;
        }
    }
    return nullptr;
}

bool operator_less(const RepairOperator& left, const RepairOperator& right) {
    if (left.name != right.name) {
        return left.name < right.name;
    }
    if (left.object != right.object) {
        return left.object < right.object;
    }
    if (left.pointer.value != right.pointer.value) {
        return left.pointer.value < right.pointer.value;
    }
    if (left.attribute != right.attribute) {
        return left.attribute < right.attribute;
    }
    return to_hex(repair_operator_hash(left)) < to_hex(repair_operator_hash(right));
}

void add_operator(
    RepairBasis& basis,
    std::set<std::string>& seen,
    RepairOperator op,
    std::uint32_t max_candidates) {
    if (op.delta.ops.empty() || basis.operators.size() >= max_candidates) {
        return;
    }
    const auto hash = to_hex(repair_operator_hash(op));
    if (seen.insert(hash).second) {
        basis.operators.push_back(std::move(op));
    }
}

RepairBasis canonical_repair_basis(
    const CommitRecord& record,
    const WorldSnapshot& before,
    const WorldSnapshot& current,
    std::uint32_t max_candidates) {
    RepairBasis basis;
    std::set<std::string> seen;
    const auto focus_objects = violation_objects(record);
    auto focus_pointers = violation_pointers(record);
    if (focus_pointers.empty()) {
        for (const auto& pointer : current.pointers) {
            if (active_at(pointer, current.epoch)) {
                focus_pointers.push_back(pointer.id);
            }
        }
    }
    sort_pointers(focus_pointers);

    for (const auto pointer_id : focus_pointers) {
        if (basis.operators.size() >= max_candidates) {
            break;
        }
        const auto* pointer = current.pointer(pointer_id);
        if (pointer == nullptr) {
            continue;
        }
        if (active_at(*pointer, current.epoch)) {
            Delta expire;
            expire.append_unlink(PointerRemove{pointer_id});
            add_operator(
                basis,
                seen,
                RepairOperator{"expire_pointer", std::move(expire), {}, pointer_id, {}},
                max_candidates);
        }
        if (!std::isfinite(pointer->weight.value) || pointer->weight.value > 1.0) {
            Delta lower;
            lower.append_set_pointer_weight(pointer_id, Weight{1.0});
            add_operator(
                basis,
                seen,
                RepairOperator{"lower_weight_to_bound", std::move(lower), {}, pointer_id, "weight"},
                max_candidates);
        }
        for (const auto& attribute : pointer->attributes) {
            if (basis.operators.size() >= max_candidates) {
                break;
            }
            Delta remove;
            remove.append_remove_pointer_attribute(pointer_id, attribute.key);
            add_operator(
                basis,
                seen,
                RepairOperator{"remove_attribute", std::move(remove), {}, pointer_id, attribute.key},
                max_candidates);
        }
        if (const auto* previous = before.pointer(pointer_id); previous != nullptr) {
            for (const auto& attribute : pointer->attributes) {
                if (basis.operators.size() >= max_candidates) {
                    break;
                }
                const auto* old = attribute_named(previous->attributes, attribute.key);
                if (old == nullptr) {
                    continue;
                }
                if (!(old->value == attribute.value)) {
                    Delta restore;
                    restore.append_set_pointer_attribute(pointer_id, *old);
                    add_operator(
                        basis,
                        seen,
                        RepairOperator{"restore_previous_attribute", std::move(restore), {}, pointer_id, attribute.key},
                        max_candidates);
                }
            }
        }
    }

    for (const auto object_id : focus_objects) {
        if (basis.operators.size() >= max_candidates) {
            break;
        }
        const auto* object = current.object(object_id);
        if (object == nullptr) {
            continue;
        }
        for (const auto& attribute : object->attributes) {
            if (basis.operators.size() >= max_candidates) {
                break;
            }
            Delta remove;
            remove.append_remove_object_attribute(ObjectRef{object_id}, attribute.key);
            add_operator(
                basis,
                seen,
                RepairOperator{"remove_attribute", std::move(remove), object_id, {}, attribute.key},
                max_candidates);
        }
        if (const auto* previous = before.object(object_id); previous != nullptr) {
            for (const auto& attribute : object->attributes) {
                if (basis.operators.size() >= max_candidates) {
                    break;
                }
                const auto* old = attribute_named(previous->attributes, attribute.key);
                if (old == nullptr || old->value == attribute.value) {
                    continue;
                }
                Delta restore;
                restore.append_set_object_attribute(ObjectRef{object_id}, *old);
                add_operator(
                    basis,
                    seen,
                    RepairOperator{"restore_previous_attribute", std::move(restore), object_id, {}, attribute.key},
                    max_candidates);
            }
        }
    }

    std::ranges::sort(basis.operators, operator_less);
    basis.basis_hash = repair_basis_hash(basis);
    return basis;
}

std::string repair_explanation(
    const RepairBasis& basis,
    std::uint32_t search_depth,
    std::size_t frontier_size,
    Hash256 witness_hash,
    std::string_view status) {
    std::ostringstream explanation;
    explanation << status
                << "; repair basis hash: " << to_hex(basis.basis_hash)
                << "; operators: " << basis.operators.size()
                << "; search depth: " << search_depth
                << "; frontier size: " << frontier_size;
    if (!empty(witness_hash)) {
        explanation << "; minimum witness operation batch hash: " << to_hex(witness_hash);
    }
    return explanation.str();
}

std::uint64_t expansion_limit(RepairSearchOptions options) noexcept {
    const auto depth = static_cast<std::uint64_t>(options.max_depth) + 1U;
    if (options.max_candidates == 0) {
        return 0;
    }
    if (depth > std::numeric_limits<std::uint64_t>::max() / options.max_candidates) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return depth * options.max_candidates;
}

std::string_view repair_status_text(RepairSolveStatus status) noexcept {
    switch (status) {
    case RepairSolveStatus::Accepted:
        return "accepted legal state; minimum bounded repair depth: 0";
    case RepairSolveStatus::VerifierUnavailable:
        return "bounded repair search unavailable because verifier was not supplied";
    case RepairSolveStatus::Found:
        return "minimum bounded repair depth found";
    case RepairSolveStatus::Exhausted:
        return "bounded canonical repair basis exhausted";
    }
    return "bounded canonical repair basis exhausted";
}

std::uint32_t explanation_depth(const RepairSolveResult& result, RepairSearchOptions options) noexcept {
    if (result.status == RepairSolveStatus::Found || result.status == RepairSolveStatus::Accepted) {
        return result.depth;
    }
    return options.max_depth;
}

RepairSolveResult solve_repair(
    RepairBasis basis,
    const CommitRecord& record,
    const WorldSnapshot& before,
    const WorldSnapshot& target,
    const Verifier* verifier,
    RepairSearchOptions options) {
    RepairSolveResult result;
    result.basis = std::move(basis);

    if (record.violations.empty()) {
        return result;
    }
    if (verifier == nullptr) {
        result.status = RepairSolveStatus::VerifierUnavailable;
        result.depth = options.max_depth + 1U;
        return result;
    }
    if (legal_state(*verifier, before, target)) {
        return result;
    }

    struct Node {
        WorldSnapshot snapshot;
        Delta witness_delta;
        std::uint32_t depth{0};
        std::vector<Hash256> witness_ops;
    };

    std::deque<Node> queue;
    std::set<std::string> seen_snapshots;
    queue.push_back(Node{target, {}, 0, {}});
    seen_snapshots.insert(to_hex(target.canonical_hash()));
    result.frontier_size = queue.size();
    std::uint64_t expansions = 0;
    const auto max_expansions = expansion_limit(options);

    while (!queue.empty()) {
        result.frontier_size = std::max(result.frontier_size, queue.size());
        auto node = std::move(queue.front());
        queue.pop_front();
        if (node.depth >= options.max_depth) {
            continue;
        }
        for (const auto& op : result.basis.operators) {
            if (max_expansions > 0 && expansions >= max_expansions) {
                result.status = RepairSolveStatus::Exhausted;
                result.depth = options.max_depth + 1U;
                return result;
            }
            expansions += 1;

            const auto next = apply_delta_to_snapshot(node.snapshot, op.delta);
            if (!next.has_value()) {
                continue;
            }
            auto merged = merge_sequential(target, node.witness_delta, op.delta);
            if (!merged.has_value()) {
                continue;
            }
            const auto hash = to_hex(next->canonical_hash());
            if (!seen_snapshots.insert(hash).second) {
                continue;
            }
            auto witness_ops = node.witness_ops;
            witness_ops.push_back(repair_operator_hash(op));
            const auto next_depth = node.depth + 1U;

            if (legal_state(*verifier, before, *next)) {
                result.status = RepairSolveStatus::Found;
                result.depth = next_depth;
                result.witness.delta = std::move(*merged);
                result.witness.operation_hashes = std::move(witness_ops);
                result.witness.operation_batch_hash = repair_operation_batch_hash(result.witness.operation_hashes);
                return result;
            }

            queue.push_back(Node{*next, std::move(*merged), next_depth, std::move(witness_ops)});
        }
    }

    result.status = RepairSolveStatus::Exhausted;
    result.depth = options.max_depth + 1U;
    return result;
}

}  // namespace

Hash256 repair_operator_hash(const RepairOperator& op) {
    CanonicalWriter writer;
    writer.string("RepairOperator:v1");
    writer.string(op.name);
    write_object(writer, op.object);
    write_pointer(writer, op.pointer);
    writer.string(op.attribute);
    writer.hash(canonical_hash(op.delta));
    return sha256(writer.bytes());
}

Hash256 repair_basis_hash(RepairBasis basis) {
    std::ranges::sort(basis.operators, operator_less);
    CanonicalWriter writer;
    writer.string("RepairBasis:v1");
    writer.u64(basis.operators.size());
    for (const auto& op : basis.operators) {
        writer.hash(repair_operator_hash(op));
    }
    return sha256(writer.bytes());
}

Hash256 repair_operation_batch_hash(std::vector<Hash256> operations) {
    std::ranges::sort(operations, [](Hash256 left, Hash256 right) {
        return to_hex(left) < to_hex(right);
    });
    operations.erase(std::ranges::unique(operations).begin(), operations.end());
    CanonicalWriter writer;
    writer.string("RepairOperationBatch:v1");
    writer.u64(operations.size());
    for (const auto hash : operations) {
        writer.hash(hash);
    }
    return sha256(writer.bytes());
}

RepairSolveResult RepairSolver::solve(
    const Repository& repository,
    std::string_view,
    CommitId commit,
    const Verifier& verifier,
    RepairSearchOptions options) const {
    return solve(repository, {}, commit, &verifier, options);
}

RepairSolveResult RepairSolver::solve(
    const Repository& repository,
    std::string_view,
    CommitId commit,
    const Verifier* verifier,
    RepairSearchOptions options) const {
    const auto problem = repair_problem_for(repository, commit);
    auto basis = canonical_repair_basis(problem.record, problem.before, problem.target, options.max_candidates);
    return solve_repair(std::move(basis), problem.record, problem.before, problem.target, verifier, options);
}

MeasuredComponent RepairDistanceMeasure::measure(
    const Repository& repository,
    std::string_view,
    CommitId commit,
    const Verifier& verifier,
    RepairSearchOptions options) const {
    return measure(repository, {}, commit, &verifier, options);
}

MeasuredComponent RepairDistanceMeasure::measure(
    const Repository& repository,
    std::string_view,
    CommitId commit,
    const Verifier* verifier,
    RepairSearchOptions options) const {
    const auto problem = repair_problem_for(repository, commit);
    auto basis = canonical_repair_basis(problem.record, problem.before, problem.target, options.max_candidates);
    const auto solved = solve_repair(std::move(basis), problem.record, problem.before, problem.target, verifier, options);

    MeasuredComponent component;
    component.namespace_id = "repair";
    component.functional_id = "distance";
    component.name = "repair.distance";
    component.evidence.component = component.name;
    component.evidence.input_root = problem.record.before_root;
    component.evidence.output_root = problem.target.canonical_hash();
    component.evidence.commits.push_back(commit);
    component.evidence.objects = violation_objects(problem.record);
    component.evidence.pointers = violation_pointers(problem.record);
    component.evidence.laws = violation_laws(problem.record);
    component.value = solved.depth;
    component.evidence.explanation = repair_explanation(
        solved.basis,
        explanation_depth(solved, options),
        solved.frontier_size,
        solved.witness.operation_batch_hash,
        repair_status_text(solved.status));
    return component;
}

}  // namespace pv
