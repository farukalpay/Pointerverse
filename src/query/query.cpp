// SPDX-License-Identifier: Apache-2.0
#include "pv/query/query.hpp"

#include <algorithm>
#include <string>

#include "pv/storage/repository.hpp"

namespace pv {
namespace {

bool active_at(const PointerSnapshot& pointer, Epoch epoch) noexcept {
    return pointer.born_at <= epoch && (!pointer.expires_at.has_value() || epoch < *pointer.expires_at);
}

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
    for (const auto& object : snapshot.objects) {
        if (snapshot.type_name(object.type) == type) {
            result.objects.push_back(object.id);
        }
    }
    return result;
}

QueryResult QueryEngine::objects_by_name(const WorldSnapshot& snapshot, std::string_view name) const {
    QueryResult result;
    for (const auto& object : snapshot.objects) {
        if (object.name == name) {
            result.objects.push_back(object.id);
        }
    }
    return result;
}

QueryResult QueryEngine::links_by_relation(const WorldSnapshot& snapshot, std::string_view relation) const {
    QueryResult result;
    for (const auto& pointer : snapshot.pointers) {
        if (active_at(pointer, snapshot.epoch) && snapshot.relation_name(pointer.relation) == relation) {
            result.pointers.push_back(pointer.id);
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
    for (const auto& pointer : snapshot.pointers) {
        if (!active_at(pointer, snapshot.epoch) || snapshot.relation_name(pointer.relation) != relation) {
            continue;
        }
        if (object_name(snapshot, pointer.from) == from && object_name(snapshot, pointer.to) == to) {
            result.pointers.push_back(pointer.id);
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

    std::vector<ObjectId> frontier{root};
    result.objects.push_back(root);
    for (std::size_t level = 0; level < depth && !frontier.empty(); ++level) {
        std::vector<ObjectId> next;
        for (const auto& pointer : snapshot.pointers) {
            if (!active_at(pointer, snapshot.epoch)) {
                continue;
            }
            const auto out = (direction == "out" || direction == "both") && has_object(frontier, pointer.from);
            const auto in = (direction == "in" || direction == "both") && has_object(frontier, pointer.to);
            if (!out && !in) {
                continue;
            }
            if (!has_pointer(result.pointers, pointer.id)) {
                result.pointers.push_back(pointer.id);
            }
            const auto candidate = out ? pointer.to : pointer.from;
            if (!has_object(result.objects, candidate)) {
                result.objects.push_back(candidate);
                next.push_back(candidate);
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
    QueryResult result;
    const auto snapshot = repository.world(branch).snapshot();
    const auto name = object_name(snapshot, object);
    const auto id = to_string(object);
    for (const auto& record : repository.history(branch)) {
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

QueryResult QueryEngine::events_by_name(
    const Repository& repository,
    std::string_view branch,
    std::string_view event_name) const {
    QueryResult result;
    for (const auto& record : repository.history(branch)) {
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
