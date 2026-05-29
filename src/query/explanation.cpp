// SPDX-License-Identifier: Apache-2.0
#include "pv/query/explanation.hpp"

#include <fmt/format.h>

#include <sstream>
#include <unordered_map>
#include <vector>

#include "pv/core/delta.hpp"
#include "pv/core/operation.hpp"
#include "pv/core/value.hpp"
#include "pv/hash/canonical.hpp"
#include "pv/law/law.hpp"
#include "pv/query/query.hpp"
#include "pv/storage/content_store.hpp"
#include "pv/storage/object_codec.hpp"
#include "pv/storage/repository.hpp"

namespace pv {
namespace {

std::string short_hash(CommitId id) {
    return to_hex(id.value).substr(0, 12);
}

const ObjectSnapshot* object_by_name(const WorldSnapshot& snapshot, std::string_view name) {
    for (const auto& object : snapshot.objects) {
        if (object.name == name) {
            return &object;
        }
    }
    return nullptr;
}

const CommitRecord* commit_by_prefix(const Repository& repository, std::string_view branch, std::string_view prefix) {
    for (const auto& record : repository.history(branch)) {
        const auto hash = to_hex(record.id.value);
        if (hash.rfind(prefix, 0) == 0) {
            static thread_local CommitRecord cached;
            cached = record;
            return &cached;
        }
    }
    return nullptr;
}

bool active_at(const PointerSnapshot& pointer, Epoch epoch) noexcept {
    return pointer.born_at <= epoch && (!pointer.expires_at.has_value() || epoch < *pointer.expires_at);
}

std::string ref_name(
    const WorldSnapshot& snapshot,
    const std::unordered_map<std::uint32_t, std::string>& temps,
    const ObjectRef& ref) {
    if (const auto* id = std::get_if<ObjectId>(&ref)) {
        if (const auto* object = snapshot.object(*id)) {
            return object->name;
        }
        return to_string(*id);
    }
    const auto temp = std::get<TempObjectId>(ref);
    if (const auto iter = temps.find(temp.value); iter != temps.end()) {
        return iter->second;
    }
    return to_string(temp);
}

// Render the exact operations carried by a commit's delta, resolving type,
// relation, and object names against the post-commit snapshot. Internal
// bookkeeping ops (symbol interning, asserts, raw events) are omitted.
std::vector<std::string> summarize_delta(const WorldSnapshot& after, const Delta& delta) {
    std::unordered_map<std::uint32_t, std::string> temps;
    for (const auto& op : delta.ops) {
        if (op.kind == OperationKind::CreateObject) {
            const auto& body = std::get<CreateObjectOp>(op.body);
            temps.emplace(body.temp_id.value, body.name);
        }
    }

    std::vector<std::string> lines;
    for (const auto& op : delta.ops) {
        switch (op.kind) {
        case OperationKind::CreateObject: {
            const auto& body = std::get<CreateObjectOp>(op.body);
            lines.push_back(fmt::format("create object {} : {}", body.name, after.type_name(body.type)));
            for (const auto& attribute : body.attributes) {
                lines.push_back(fmt::format("  {}.{} = {}", body.name, attribute.key, to_string(attribute.value)));
            }
            break;
        }
        case OperationKind::SetObjectType: {
            const auto& body = std::get<SetObjectTypeOp>(op.body);
            lines.push_back(fmt::format("set type {} : {}", ref_name(after, temps, body.object), after.type_name(body.type)));
            break;
        }
        case OperationKind::SetObjectExistence: {
            const auto& body = std::get<SetObjectExistenceOp>(op.body);
            lines.push_back(fmt::format("set existence {} = {}", ref_name(after, temps, body.object), to_string(body.existence)));
            break;
        }
        case OperationKind::SetObjectAttribute: {
            const auto& body = std::get<SetObjectAttributeOp>(op.body);
            lines.push_back(fmt::format(
                "set {}.{} = {}", ref_name(after, temps, body.object), body.attribute.key, to_string(body.attribute.value)));
            break;
        }
        case OperationKind::RemoveObjectAttribute: {
            const auto& body = std::get<RemoveObjectAttributeOp>(op.body);
            lines.push_back(fmt::format("remove {}.{}", ref_name(after, temps, body.object), body.key));
            break;
        }
        case OperationKind::CreatePointer: {
            const auto& body = std::get<CreatePointerOp>(op.body);
            lines.push_back(fmt::format(
                "create link {} -> {} : {} (weight={:.6g} role={})",
                ref_name(after, temps, body.from),
                ref_name(after, temps, body.to),
                after.relation_name(body.relation),
                body.weight.value,
                to_string(body.causal_role)));
            for (const auto& attribute : body.attributes) {
                lines.push_back(fmt::format("  {} = {}", attribute.key, to_string(attribute.value)));
            }
            break;
        }
        case OperationKind::ExpirePointer: {
            const auto& body = std::get<ExpirePointerOp>(op.body);
            lines.push_back(fmt::format("expire pointer {}", to_string(body.id)));
            break;
        }
        case OperationKind::SetPointerWeight: {
            const auto& body = std::get<SetPointerWeightOp>(op.body);
            lines.push_back(fmt::format("set weight {} = {:.6g}", to_string(body.id), body.weight.value));
            break;
        }
        case OperationKind::SetPointerAttribute: {
            const auto& body = std::get<SetPointerAttributeOp>(op.body);
            lines.push_back(fmt::format(
                "set pointer {}.{} = {}", to_string(body.id), body.attribute.key, to_string(body.attribute.value)));
            break;
        }
        case OperationKind::RemovePointerAttribute: {
            const auto& body = std::get<RemovePointerAttributeOp>(op.body);
            lines.push_back(fmt::format("remove pointer {}.{}", to_string(body.id), body.key));
            break;
        }
        case OperationKind::EmitEvent:
        case OperationKind::InternType:
        case OperationKind::InternRelation:
        case OperationKind::AssertObject:
        case OperationKind::AssertPointer:
        case OperationKind::AssertFact:
            break;
        }
    }
    return lines;
}

const LawStatus* find_status(const std::vector<LawStatus>& statuses, const LawId& law) {
    for (const auto& status : statuses) {
        if (status.law == law) {
            return &status;
        }
    }
    return nullptr;
}

}  // namespace

std::string ExplanationEngine::explain_object(
    const Repository& repository,
    std::string_view branch,
    std::string_view object_name_value) const {
    const auto snapshot = repository.world(branch).snapshot();
    const auto* object = object_by_name(snapshot, object_name_value);
    if (object == nullptr) {
        return fmt::format("object {}: unavailable\n", object_name_value);
    }

    const auto commits = QueryEngine{}.commits_touching_object(repository, branch, object->id);
    std::ostringstream output;
    output << fmt::format("object {}\n", object->name);
    output << fmt::format("  id: {}\n", to_string(object->id));
    output << fmt::format("  type: {}\n", snapshot.type_name(object->type));
    output << fmt::format("  incoming: {}\n", object->incoming_count);
    output << fmt::format("  outgoing: {}\n", object->outgoing_count);
    output << "  commits:\n";
    for (const auto& commit : commits.commits) {
        output << fmt::format("    {}\n", short_hash(commit));
    }
    return output.str();
}

std::string ExplanationEngine::explain_commit(
    const Repository& repository,
    std::string_view branch,
    std::string_view commit_prefix) const {
    const auto* record = commit_by_prefix(repository, branch, commit_prefix);
    if (record == nullptr) {
        return fmt::format("commit {}: unavailable\n", commit_prefix);
    }

    const auto id = record->id;
    const auto parent = record->parent;
    const auto label = record->label;
    const auto origin = record->origin;
    const bool accepted = record->accepted;
    const auto before_epoch = record->before_epoch;
    const auto after_epoch = record->after_epoch;

    std::ostringstream output;
    output << fmt::format("commit {}\n", short_hash(id));
    output << fmt::format("  label: {}\n", label.empty() ? "(unlabeled)" : label);
    output << fmt::format("  origin: {}\n", to_string(origin));
    output << fmt::format("  accepted: {}\n", accepted ? "yes" : "no");
    output << fmt::format("  epoch: {} -> {}\n", before_epoch.value, after_epoch.value);

    try {
        const auto stored = repository.objects().get_canonical<StoredCommit>(id.value);
        const auto after = repository.objects().get_canonical<WorldSnapshot>(stored.after_snapshot_object);
        const auto delta = repository.objects().get_canonical<Delta>(stored.delta_object);
        const auto statuses = repository.objects().get_canonical<std::vector<LawStatus>>(stored.law_status_object);
        const auto violations = repository.objects().get_canonical<std::vector<LawViolation>>(stored.violation_object);

        const auto lines = summarize_delta(after, delta);
        output << fmt::format("  delta: {} change(s)\n", lines.size());
        constexpr std::size_t max_lines = 40;
        for (std::size_t index = 0; index < lines.size(); ++index) {
            if (index == max_lines) {
                output << fmt::format("    (+{} more)\n", lines.size() - max_lines);
                break;
            }
            output << "    " << lines[index] << "\n";
        }

        std::vector<LawStatus> parent_statuses;
        if (parent.has_value()) {
            try {
                const auto parent_stored = repository.objects().get_canonical<StoredCommit>(parent->value);
                parent_statuses =
                    repository.objects().get_canonical<std::vector<LawStatus>>(parent_stored.law_status_object);
            } catch (const std::exception&) {
                // Parent law material unavailable: report current state without a diff.
            }
        }

        if (!statuses.empty()) {
            output << "  laws:\n";
            for (const auto& status : statuses) {
                std::string change = status.passed ? "stable" : "broken";
                if (parent.has_value()) {
                    const auto* prior = find_status(parent_statuses, status.law);
                    if (prior == nullptr) {
                        change = status.passed ? "new" : "new, broken";
                    } else if (prior->passed && !status.passed) {
                        change = "broke";
                    } else if (!prior->passed && status.passed) {
                        change = "recovered";
                    } else {
                        change = "stable";
                    }
                }
                output << fmt::format(
                    "    law.{} {} ({}) magnitude={:.6g}\n",
                    status.law,
                    status.passed ? std::string{"passed"} : to_string(status.severity),
                    change,
                    status.magnitude);
            }
        }

        for (const auto& violation : violations) {
            output << fmt::format(
                "  violation: law.{} {} {}\n", violation.law, to_string(violation.severity), violation.explanation);
        }
    } catch (const std::exception&) {
        // Stored commit material unavailable: fall back to the record header.
    }

    return output.str();
}

std::string ExplanationEngine::why_relation(
    const Repository& repository,
    std::string_view branch,
    std::string_view from,
    std::string_view relation,
    std::string_view to) const {
    const auto snapshot = repository.world(branch).snapshot();
    std::ostringstream output;
    output << fmt::format("why {} {} {}\n", from, relation, to);

    bool found = false;
    for (const auto& pointer : snapshot.pointers) {
        if (!active_at(pointer, snapshot.epoch)) {
            continue;
        }
        const auto* from_object = snapshot.object(pointer.from);
        const auto* to_object = snapshot.object(pointer.to);
        if (from_object == nullptr || to_object == nullptr) {
            continue;
        }
        if (from_object->name == from && to_object->name == to && snapshot.relation_name(pointer.relation) == relation) {
            found = true;
            output << fmt::format("  pointer: {}\n", to_string(pointer.id));
            output << fmt::format("  born_at: epoch {}\n", pointer.born_at.value);
            const auto commits = QueryEngine{}.commits_touching_object(repository, branch, pointer.to);
            output << "  related commits:\n";
            for (const auto& commit : commits.commits) {
                output << fmt::format("    {}\n", short_hash(commit));
            }
        }
    }

    if (!found) {
        output << "  relation not found\n";
    }
    return output.str();
}

}  // namespace pv
