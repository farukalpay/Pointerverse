// SPDX-License-Identifier: Apache-2.0
#include "pv/measure/history_measure.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <variant>
#include <vector>

#include "pv/core/delta.hpp"
#include "pv/storage/object_codec.hpp"
#include "pv/storage/repository.hpp"

namespace pv {
namespace {

std::uint64_t saturating_add(std::uint64_t left, std::uint64_t right) noexcept {
    constexpr auto max = std::numeric_limits<std::uint64_t>::max();
    if (max - left < right) {
        return max;
    }
    return left + right;
}

std::optional<ObjectId> object_named(const WorldSnapshot& snapshot, std::string_view name) {
    for (const auto& object : snapshot.objects) {
        if (object.name == name) {
            return object.id;
        }
    }
    return std::nullopt;
}

std::optional<ObjectId> resolve_ref(
    const WorldSnapshot& after,
    const std::map<std::uint32_t, ObjectId>& temps,
    const ObjectRef& ref) {
    if (const auto* id = std::get_if<ObjectId>(&ref)) {
        return after.contains(*id) ? std::optional<ObjectId>{*id} : std::nullopt;
    }
    const auto temp = std::get<TempObjectId>(ref);
    const auto iter = temps.find(temp.value);
    if (iter == temps.end()) {
        return std::nullopt;
    }
    return iter->second;
}

std::string object_type(const WorldSnapshot& snapshot, ObjectId object) {
    const auto* item = snapshot.object(object);
    return item == nullptr ? "unknown" : snapshot.type_name(item->type);
}

struct CommitSignatures {
    std::vector<std::string> operations;
    std::vector<std::string> relations;
    std::vector<std::string> type_pairs;
    std::vector<std::string> touched_objects;
};

void sort_unique(std::vector<std::string>& values) {
    std::ranges::sort(values);
    values.erase(std::ranges::unique(values).begin(), values.end());
}

CommitSignatures signatures_for(const CommitRecord& record, const Delta& delta, const WorldSnapshot& after) {
    CommitSignatures signatures;
    std::map<std::uint32_t, ObjectId> temps;

    auto add_touched = [&](ObjectId object) {
        if (const auto* item = after.object(object); item != nullptr) {
            signatures.touched_objects.push_back(item->name);
        }
    };

    for (const auto& op : delta.ops) {
        switch (op.kind) {
        case OperationKind::CreateObject: {
            const auto& body = std::get<CreateObjectOp>(op.body);
            signatures.operations.push_back("CreateObject(" + after.type_name(body.type) + ")");
            if (const auto object = object_named(after, body.name); object.has_value()) {
                temps.emplace(body.temp_id.value, *object);
                add_touched(*object);
            }
            break;
        }
        case OperationKind::SetObjectAttribute: {
            const auto& body = std::get<SetObjectAttributeOp>(op.body);
            const auto object = resolve_ref(after, temps, body.object);
            const auto type = object.has_value() ? object_type(after, *object) : "unknown";
            signatures.operations.push_back("SetObjectAttribute(" + type + "," + body.attribute.key + ")");
            if (object.has_value()) {
                add_touched(*object);
            }
            break;
        }
        case OperationKind::RemoveObjectAttribute: {
            const auto& body = std::get<RemoveObjectAttributeOp>(op.body);
            const auto object = resolve_ref(after, temps, body.object);
            const auto type = object.has_value() ? object_type(after, *object) : "unknown";
            signatures.operations.push_back("RemoveObjectAttribute(" + type + "," + body.key + ")");
            if (object.has_value()) {
                add_touched(*object);
            }
            break;
        }
        case OperationKind::CreatePointer: {
            const auto& body = std::get<CreatePointerOp>(op.body);
            const auto from = resolve_ref(after, temps, body.from);
            const auto to = resolve_ref(after, temps, body.to);
            const auto relation = after.relation_name(body.relation);
            const auto from_type = from.has_value() ? object_type(after, *from) : "unknown";
            const auto to_type = to.has_value() ? object_type(after, *to) : "unknown";
            signatures.operations.push_back("CreatePointer(" + from_type + "," + to_type + "," + relation + ")");
            signatures.relations.push_back(relation);
            signatures.type_pairs.push_back(from_type + "->" + to_type + ":" + relation);
            if (from.has_value()) {
                add_touched(*from);
            }
            if (to.has_value()) {
                add_touched(*to);
            }
            break;
        }
        case OperationKind::SetObjectType:
            signatures.operations.push_back("SetObjectType");
            break;
        case OperationKind::SetObjectExistence:
            signatures.operations.push_back("SetObjectExistence");
            break;
        case OperationKind::ExpirePointer:
            signatures.operations.push_back("ExpirePointer");
            break;
        case OperationKind::SetPointerWeight:
            signatures.operations.push_back("SetPointerWeight");
            break;
        case OperationKind::SetPointerAttribute:
            signatures.operations.push_back("SetPointerAttribute");
            break;
        case OperationKind::RemovePointerAttribute:
            signatures.operations.push_back("RemovePointerAttribute");
            break;
        case OperationKind::EmitEvent:
            signatures.operations.push_back("EmitEvent");
            break;
        case OperationKind::InternType:
        case OperationKind::InternRelation:
        case OperationKind::AssertObject:
        case OperationKind::AssertPointer:
        case OperationKind::AssertFact:
            break;
        }
    }

    for (const auto& event : record.events) {
        signatures.operations.push_back("event:" + event.event);
        for (const auto* field : {"object", "from", "to", "actor"}) {
            const auto iter = event.fields.find(field);
            if (iter != event.fields.end() && !iter->second.empty()) {
                signatures.touched_objects.push_back(iter->second);
            }
        }
        if (const auto iter = event.fields.find("relation"); iter != event.fields.end() && !iter->second.empty()) {
            signatures.relations.push_back(iter->second);
        }
        const auto from_type = event.fields.find("from_type");
        const auto to_type = event.fields.find("to_type");
        const auto relation = event.fields.find("relation");
        if (from_type != event.fields.end() && to_type != event.fields.end() && relation != event.fields.end()) {
            signatures.type_pairs.push_back(from_type->second + "->" + to_type->second + ":" + relation->second);
        }
    }

    sort_unique(signatures.operations);
    sort_unique(signatures.relations);
    sort_unique(signatures.type_pairs);
    sort_unique(signatures.touched_objects);
    return signatures;
}

void add_counts(std::map<std::string, std::uint64_t>& counts, const std::vector<std::string>& values) {
    for (const auto& value : values) {
        counts[value] += 1;
    }
}

std::uint64_t rarity_sum(
    const std::map<std::string, std::uint64_t>& counts,
    const std::vector<std::string>& values,
    std::uint64_t total) {
    const auto vocab = std::max<std::uint64_t>(counts.size() + values.size(), 1);
    const auto denominator = total + vocab;
    std::uint64_t out = 0;
    for (const auto& value : values) {
        const auto iter = counts.find(value);
        const auto numerator = (iter == counts.end() ? 0 : iter->second) + 1;
        out = saturating_add(out, neg_log2_scaled(numerator, denominator));
    }
    return out;
}

std::uint64_t unseen_count(const std::map<std::string, std::uint64_t>& counts, const std::vector<std::string>& values) {
    return static_cast<std::uint64_t>(std::ranges::count_if(values, [&](const auto& value) {
        return !counts.contains(value);
    }));
}

}  // namespace

std::uint64_t neg_log2_scaled(std::uint64_t numerator, std::uint64_t denominator) noexcept {
    if (numerator == 0) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    if (denominator <= numerator) {
        return 0;
    }
    constexpr std::uint64_t scale = 1024;
    std::uint64_t out = 0;
    auto value = numerator;
    while (value < denominator) {
        if (value > std::numeric_limits<std::uint64_t>::max() / 2U) {
            return saturating_add(out, scale);
        }
        value *= 2U;
        out = saturating_add(out, scale);
    }
    return out;
}

MeasuredComponent HistorySurpriseMeasure::measure(
    const Repository& repository,
    std::string_view branch,
    CommitId commit) const {
    HistoryFrequency frequency;
    const auto history = repository.backend().history(branch);
    CommitRecord target;
    bool found = false;
    for (const auto& record : history) {
        if (record.id == commit) {
            target = record;
            found = true;
            break;
        }
        if (record.origin == TransactionOrigin::Internal) {
            continue;
        }
        const auto stored = repository.backend().stored_commit(record.id);
        const auto delta = repository.objects().get_canonical<Delta>(stored.delta_object);
        const auto after = repository.backend().snapshot(record.id);
        const auto signatures = signatures_for(record, delta, after);
        frequency.total_commits += 1;
        add_counts(frequency.operation_counts, signatures.operations);
        add_counts(frequency.relation_counts, signatures.relations);
        add_counts(frequency.type_pair_counts, signatures.type_pairs);
        add_counts(frequency.touched_object_counts, signatures.touched_objects);
    }
    if (!found) {
        target = repository.backend().commit_record(commit);
    }

    const auto stored = repository.backend().stored_commit(commit);
    const auto delta = repository.objects().get_canonical<Delta>(stored.delta_object);
    const auto after = repository.backend().snapshot(commit);
    const auto signatures = signatures_for(target, delta, after);

    MeasuredComponent component;
    component.name = "surprise";
    component.evidence.component = component.name;
    component.evidence.input_root = target.before_root;
    component.evidence.output_root = target.trace_hash;
    component.evidence.commits.push_back(commit);
    component.value = rarity_sum(frequency.operation_counts, signatures.operations, frequency.total_commits);
    component.value = saturating_add(
        component.value,
        rarity_sum(frequency.relation_counts, signatures.relations, frequency.total_commits));
    component.value = saturating_add(
        component.value,
        rarity_sum(frequency.type_pair_counts, signatures.type_pairs, frequency.total_commits));
    component.value = saturating_add(
        component.value,
        rarity_sum(frequency.touched_object_counts, signatures.touched_objects, frequency.total_commits));

    const auto rare_operations = unseen_count(frequency.operation_counts, signatures.operations);
    std::ostringstream explanation;
    explanation << "prior commits: " << frequency.total_commits
                << "; rare operation signatures: " << rare_operations
                << "; relation signatures: " << signatures.relations.size()
                << "; type-pair signatures: " << signatures.type_pairs.size()
                << "; touched-object novelty: " << unseen_count(frequency.touched_object_counts, signatures.touched_objects);
    component.evidence.explanation = explanation.str();
    return component;
}

}  // namespace pv
