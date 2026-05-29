// SPDX-License-Identifier: Apache-2.0
#include "pv/query/query.hpp"

#include <algorithm>
#include <string>

#include "pv/core/world_index.hpp"
#include "pv/storage/repository.hpp"

namespace pv {
namespace {

bool has_object(const std::vector<ObjectId>& objects, ObjectId object) {
    return std::ranges::find(objects, object) != objects.end();
}

bool has_pointer(const std::vector<PointerId>& pointers, PointerId pointer) {
    return std::ranges::find(pointers, pointer) != pointers.end();
}

std::string object_name(const WorldSnapshot& snapshot, ObjectId object) {
    if (const auto* view = snapshot.object(object); view != nullptr) {
        return view->name;
    }
    return to_string(object);
}

bool event_touches_name(const TraceEvent& event, std::string_view name, std::string_view id) {
    for (const auto& key : {"object", "from", "to"}) {
        const auto iter = event.fields.find(key);
        if (iter != event.fields.end() && (iter->second == name || iter->second == id)) {
            return true;
        }
    }
    return false;
}

}  // namespace

QueryResult QueryEngine::objects_by_type(const WorldSnapshot& snapshot, std::string_view type) const {
    QueryResult result;
    WorldIndex index;
    index.rebuild(snapshot);
    for (const auto& [id, name] : snapshot.type_names) {
        if (name == type) {
            const auto objects = index.objects_by_type(TypeId{id});
            result.objects.assign(objects.begin(), objects.end());
            return result;
        }
    }
    return result;
}

QueryResult QueryEngine::objects_by_name(const WorldSnapshot& snapshot, std::string_view name) const {
    QueryResult result;
    WorldIndex index;
    index.rebuild(snapshot);
    if (const auto object = index.object_by_name(name); object.has_value()) {
        result.objects.push_back(*object);
    }
    return result;
}

QueryResult QueryEngine::links_by_relation(const WorldSnapshot& snapshot, std::string_view relation) const {
    QueryResult result;
    WorldIndex index;
    index.rebuild(snapshot);
    for (const auto& [id, name] : snapshot.relation_names) {
        if (name == relation) {
            const auto pointers = index.relation(RelationType{id});
            result.pointers.assign(pointers.begin(), pointers.end());
            return result;
        }
    }
    return result;
}

QueryResult QueryEngine::links_between(
    const WorldSnapshot& snapshot,
    std::string_view from,
        std::string_view relation,
        std::string_view to) const {
    QueryResult result;
    WorldIndex index;
    index.rebuild(snapshot);
    std::optional<RelationType> relation_type;
    for (const auto& [id, name] : snapshot.relation_names) {
        if (name == relation) {
            relation_type = RelationType{id};
            break;
        }
    }
    if (!relation_type.has_value()) {
        return result;
    }
    for (const auto pointer_id : index.relation(*relation_type)) {
        const auto* pointer = snapshot.pointer(pointer_id);
        if (pointer == nullptr) {
            continue;
        }
        if (object_name(snapshot, pointer->from) == from && object_name(snapshot, pointer->to) == to) {
            result.pointers.push_back(pointer->id);
        }
    }
    return result;
}

QueryResult QueryEngine::causal_cone(
    const WorldSnapshot& snapshot,
    ObjectId root,
    std::size_t depth,
    std::string_view direction) const {
    QueryResult result;
    if (!snapshot.contains(root)) {
        return result;
    }

    WorldIndex index;
    index.rebuild(snapshot);
    std::vector<ObjectId> frontier{root};
    result.objects.push_back(root);
    for (std::size_t level = 0; level < depth && !frontier.empty(); ++level) {
        std::vector<ObjectId> next;
        for (const auto object : frontier) {
            if (direction == "out" || direction == "both") {
                for (const auto pointer_id : index.outgoing(object)) {
                    const auto* pointer = snapshot.pointer(pointer_id);
                    if (pointer == nullptr) {
                        continue;
                    }
                    if (!has_pointer(result.pointers, pointer->id)) {
                        result.pointers.push_back(pointer->id);
                    }
                    if (!has_object(result.objects, pointer->to)) {
                        result.objects.push_back(pointer->to);
                        next.push_back(pointer->to);
                    }
                }
            }
            if (direction == "in" || direction == "both") {
                for (const auto pointer_id : index.incoming(object)) {
                    const auto* pointer = snapshot.pointer(pointer_id);
                    if (pointer == nullptr) {
                        continue;
                    }
                    if (!has_pointer(result.pointers, pointer->id)) {
                        result.pointers.push_back(pointer->id);
                    }
                    if (!has_object(result.objects, pointer->from)) {
                        result.objects.push_back(pointer->from);
                        next.push_back(pointer->from);
                    }
                }
            }
        }
        frontier = std::move(next);
    }
    return result;
}

QueryResult QueryEngine::commits_touching_object(
    const Repository& repository,
    std::string_view branch,
    ObjectId object) const {
    return RepositoryQueryEngine{}.commits_touching_object(repository, branch, object);
}

QueryResult QueryEngine::events_by_name(
    const Repository& repository,
    std::string_view branch,
    std::string_view event_name) const {
    return RepositoryQueryEngine{}.events_by_name(repository, branch, event_name);
}

QueryResult RepositoryQueryEngine::objects_by_type(
    const Repository& repository,
    std::string_view branch,
    std::string_view type) const {
    QueryResult result;
    result.objects = repository.backend().world_index().objects_by_type(branch, type);
    return result;
}

QueryResult RepositoryQueryEngine::objects_by_name(
    const Repository& repository,
    std::string_view branch,
    std::string_view name) const {
    QueryResult result;
    if (const auto object = repository.backend().world_index().object_by_name(branch, name); object.has_value()) {
        result.objects.push_back(*object);
    }
    return result;
}

QueryResult RepositoryQueryEngine::links_by_relation(
    const Repository& repository,
    std::string_view branch,
    std::string_view relation) const {
    QueryResult result;
    result.pointers = repository.backend().world_index().links_by_relation(branch, relation);
    return result;
}

QueryResult RepositoryQueryEngine::commits_touching_object(
    const Repository& repository,
    std::string_view branch,
    ObjectId object) const {
    QueryResult result;
    const auto snapshot = repository.backend().snapshot(branch);
    const auto name = object_name(snapshot, object);
    const auto id = to_string(object);
    for (const auto commit : repository.backend().events().commits_touching_object(branch, object)) {
        const auto record = repository.backend().commit_record(commit);
        for (const auto& event : record.events) {
            if (event_touches_name(event, name, id)) {
                result.commits.push_back(record.id);
                result.events.push_back(event);
                break;
            }
        }
    }
    return result;
}

QueryResult RepositoryQueryEngine::events_by_name(
    const Repository& repository,
    std::string_view branch,
    std::string_view event_name) const {
    QueryResult result;
    for (const auto commit : repository.backend().events().commits_for_event(branch, event_name)) {
        const auto record = repository.backend().commit_record(commit);
        for (const auto& event : record.events) {
            if (event.event == event_name) {
                result.commits.push_back(record.id);
                result.events.push_back(event);
            }
        }
    }
    return result;
}

}  // namespace pv
