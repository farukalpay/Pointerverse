// SPDX-License-Identifier: Apache-2.0
#include "pv/measure/repair_measure.hpp"

#include <algorithm>
#include <deque>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "pv/core/delta.hpp"
#include "pv/hash/canonical.hpp"
#include "pv/storage/object_codec.hpp"
#include "pv/storage/repository.hpp"

namespace pv {
namespace {

bool active_at(const PointerSnapshot& pointer, Epoch epoch) noexcept {
    return pointer.born_at <= epoch && (!pointer.expires_at.has_value() || epoch < *pointer.expires_at);
}

std::vector<ObjectId> violation_objects(const CommitRecord& record) {
    std::vector<ObjectId> objects;
    for (const auto& violation : record.violations) {
        objects.insert(objects.end(), violation.objects.begin(), violation.objects.end());
    }
    std::ranges::sort(objects, [](ObjectId left, ObjectId right) {
        if (left.index != right.index) {
            return left.index < right.index;
        }
        return left.generation < right.generation;
    });
    objects.erase(std::ranges::unique(objects).begin(), objects.end());
    return objects;
}

std::vector<PointerId> violation_pointers(const CommitRecord& record) {
    std::vector<PointerId> pointers;
    for (const auto& violation : record.violations) {
        pointers.insert(pointers.end(), violation.pointers.begin(), violation.pointers.end());
    }
    std::ranges::sort(pointers, {}, &PointerId::value);
    pointers.erase(std::ranges::unique(pointers).begin(), pointers.end());
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

bool legal_state(const Verifier& verifier, const WorldSnapshot& before, const WorldSnapshot& after) {
    const auto result = verifier.check(LawCheckContext{before, Delta{}, after});
    return result.violations.empty();
}

void add_unique_delta(std::vector<Delta>& candidates, std::set<std::string>& seen, Delta delta) {
    if (delta.ops.empty()) {
        return;
    }
    const auto key = to_hex(canonical_hash(delta));
    if (seen.insert(key).second) {
        candidates.push_back(std::move(delta));
    }
}

const Attribute* attribute_named(const std::vector<Attribute>& attributes, std::string_view name) {
    for (const auto& attribute : attributes) {
        if (attribute.key == name) {
            return &attribute;
        }
    }
    return nullptr;
}

std::vector<Delta> repair_candidates(
    const CommitRecord& record,
    const WorldSnapshot& before,
    const WorldSnapshot& current,
    std::uint32_t max_candidates) {
    std::vector<Delta> candidates;
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
    std::ranges::sort(focus_pointers, {}, &PointerId::value);
    focus_pointers.erase(std::ranges::unique(focus_pointers).begin(), focus_pointers.end());

    auto limited = [&] {
        return candidates.size() >= max_candidates;
    };

    for (const auto pointer_id : focus_pointers) {
        if (limited()) {
            break;
        }
        const auto* pointer = current.pointer(pointer_id);
        if (pointer == nullptr) {
            continue;
        }
        if (active_at(*pointer, current.epoch)) {
            Delta expire;
            expire.append_unlink(PointerRemove{pointer_id});
            add_unique_delta(candidates, seen, std::move(expire));
        }
        for (const auto& attribute : pointer->attributes) {
            if (limited()) {
                break;
            }
            Delta remove;
            remove.append_remove_pointer_attribute(pointer_id, attribute.key);
            add_unique_delta(candidates, seen, std::move(remove));
        }
        if (const auto* previous = before.pointer(pointer_id); previous != nullptr) {
            for (const auto& attribute : pointer->attributes) {
                if (limited()) {
                    break;
                }
                const auto* old = attribute_named(previous->attributes, attribute.key);
                if (old == nullptr) {
                    Delta remove;
                    remove.append_remove_pointer_attribute(pointer_id, attribute.key);
                    add_unique_delta(candidates, seen, std::move(remove));
                } else if (!(old->value == attribute.value)) {
                    Delta restore;
                    restore.append_set_pointer_attribute(pointer_id, *old);
                    add_unique_delta(candidates, seen, std::move(restore));
                }
            }
        }
    }

    for (const auto object_id : focus_objects) {
        if (limited()) {
            break;
        }
        const auto* object = current.object(object_id);
        if (object == nullptr) {
            continue;
        }
        for (const auto& attribute : object->attributes) {
            if (limited()) {
                break;
            }
            Delta remove;
            remove.append_remove_object_attribute(ObjectRef{object_id}, attribute.key);
            add_unique_delta(candidates, seen, std::move(remove));
        }
        if (const auto* previous = before.object(object_id); previous != nullptr) {
            for (const auto& attribute : object->attributes) {
                if (limited()) {
                    break;
                }
                const auto* old = attribute_named(previous->attributes, attribute.key);
                if (old == nullptr) {
                    Delta remove;
                    remove.append_remove_object_attribute(ObjectRef{object_id}, attribute.key);
                    add_unique_delta(candidates, seen, std::move(remove));
                } else if (!(old->value == attribute.value)) {
                    Delta restore;
                    restore.append_set_object_attribute(ObjectRef{object_id}, *old);
                    add_unique_delta(candidates, seen, std::move(restore));
                }
            }
        }
    }

    return candidates;
}

}  // namespace

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
    const auto record = repository.backend().commit_record(commit);
    const auto stored = repository.backend().stored_commit(commit);
    const auto delta = repository.objects().get_canonical<Delta>(stored.delta_object);
    auto target = repository.backend().snapshot(commit);
    const auto before = before_snapshot_for(repository, record, target);
    if (!record.accepted) {
        target = before;
    }
    if (!record.accepted) {
        if (const auto predicted = apply_delta_to_snapshot(before, delta); predicted.has_value()) {
            target = *predicted;
        }
    }

    MeasuredComponent component;
    component.name = "repair";
    component.evidence.component = component.name;
    component.evidence.input_root = record.before_root;
    component.evidence.output_root = target.canonical_hash();
    component.evidence.commits.push_back(commit);
    component.evidence.objects = violation_objects(record);
    component.evidence.pointers = violation_pointers(record);
    component.evidence.laws = violation_laws(record);

    if (record.violations.empty()) {
        component.evidence.explanation = "accepted legal state; minimum bounded repair depth: 0";
        return component;
    }
    if (verifier == nullptr) {
        component.value = static_cast<std::uint64_t>(options.max_depth) + 1U;
        component.evidence.explanation = "bounded repair search unavailable because verifier was not supplied";
        return component;
    }
    if (legal_state(*verifier, before, target)) {
        component.evidence.explanation = "accepted legal state; minimum bounded repair depth: 0";
        return component;
    }

    struct Node {
        WorldSnapshot snapshot;
        std::uint32_t depth{0};
    };
    std::deque<Node> queue;
    std::set<std::string> seen_snapshots;
    queue.push_back(Node{target, 0});
    seen_snapshots.insert(to_hex(target.canonical_hash()));
    std::uint32_t candidates_seen = 0;

    while (!queue.empty()) {
        const auto node = std::move(queue.front());
        queue.pop_front();
        if (node.depth >= options.max_depth) {
            continue;
        }
        const auto candidates = repair_candidates(record, before, node.snapshot, options.max_candidates);
        for (const auto& candidate : candidates) {
            if (candidates_seen >= options.max_candidates) {
                component.value = static_cast<std::uint64_t>(options.max_depth) + 1U;
                component.evidence.explanation = "bounded repair search exhausted";
                return component;
            }
            candidates_seen += 1;
            const auto next = apply_delta_to_snapshot(node.snapshot, candidate);
            if (!next.has_value()) {
                continue;
            }
            const auto hash = to_hex(next->canonical_hash());
            if (!seen_snapshots.insert(hash).second) {
                continue;
            }
            const auto next_depth = node.depth + 1U;
            if (legal_state(*verifier, before, *next)) {
                component.value = next_depth;
                std::ostringstream explanation;
                explanation << "minimum bounded repair depth: " << next_depth;
                component.evidence.explanation = explanation.str();
                return component;
            }
            queue.push_back(Node{*next, next_depth});
        }
    }

    component.value = static_cast<std::uint64_t>(options.max_depth) + 1U;
    component.evidence.explanation = "bounded repair search exhausted";
    return component;
}

}  // namespace pv
