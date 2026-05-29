// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "pv/core/id.hpp"
#include "pv/core/pointer.hpp"
#include "pv/core/snapshot.hpp"
#include "pv/runtime/ids.hpp"
#include "pv/trace/event.hpp"

namespace pv {

class Repository;

struct QueryResult {
    std::vector<ObjectId> objects;
    std::vector<PointerId> pointers;
    std::vector<CommitId> commits;
    std::vector<TraceEvent> events;
};

class QueryEngine {
public:
    [[nodiscard]] QueryResult objects_by_type(const WorldSnapshot& snapshot, std::string_view type) const;
    [[nodiscard]] QueryResult objects_by_name(const WorldSnapshot& snapshot, std::string_view name) const;
    [[nodiscard]] QueryResult links_by_relation(const WorldSnapshot& snapshot, std::string_view relation) const;
    [[nodiscard]] QueryResult links_between(
        const WorldSnapshot& snapshot,
        std::string_view from,
        std::string_view relation,
        std::string_view to) const;
    [[nodiscard]] QueryResult causal_cone(
        const WorldSnapshot& snapshot,
        ObjectId root,
        std::size_t depth,
        std::string_view direction = "both") const;
    [[nodiscard]] QueryResult commits_touching_object(
        const Repository& repository,
        std::string_view branch,
        ObjectId object) const;
    [[nodiscard]] QueryResult events_by_name(
        const Repository& repository,
        std::string_view branch,
        std::string_view event_name) const;
};

class RepositoryQueryEngine {
public:
    [[nodiscard]] QueryResult objects_by_type(
        const Repository& repository,
        std::string_view branch,
        std::string_view type) const;
    [[nodiscard]] QueryResult objects_by_name(
        const Repository& repository,
        std::string_view branch,
        std::string_view name) const;
    [[nodiscard]] QueryResult links_by_relation(
        const Repository& repository,
        std::string_view branch,
        std::string_view relation) const;
    [[nodiscard]] QueryResult commits_touching_object(
        const Repository& repository,
        std::string_view branch,
        ObjectId object) const;
    [[nodiscard]] QueryResult events_by_name(
        const Repository& repository,
        std::string_view branch,
        std::string_view event_name) const;
};

}  // namespace pv
