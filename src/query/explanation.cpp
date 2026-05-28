// SPDX-License-Identifier: Apache-2.0
#include "pv/query/explanation.hpp"

#include <fmt/format.h>

#include <sstream>

#include "pv/hash/canonical.hpp"
#include "pv/query/query.hpp"
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

    std::ostringstream output;
    output << fmt::format("commit {}\n", short_hash(record->id));
    output << fmt::format("  label: {}\n", record->label.empty() ? "(unlabeled)" : record->label);
    output << fmt::format("  origin: {}\n", to_string(record->origin));
    output << fmt::format("  accepted: {}\n", record->accepted ? "yes" : "no");
    output << fmt::format("  epoch: {} -> {}\n", record->before_epoch.value, record->after_epoch.value);
    output << fmt::format("  events: {}\n", record->events.size());
    for (const auto& violation : record->violations) {
        output << fmt::format("  violation: law.{} {} {}\n", violation.law, to_string(violation.severity), violation.explanation);
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
