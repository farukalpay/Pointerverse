// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace pv {

class ProjectionStore;

struct RelationProjectionEntry {
    std::string relation;
    std::size_t occurrences{0};
    std::vector<std::string> evidence_event_ids;

    friend bool operator==(const RelationProjectionEntry&, const RelationProjectionEntry&) = default;
};

class RelationProjection {
public:
    [[nodiscard]] std::vector<RelationProjectionEntry> project(
        const ProjectionStore& store,
        std::string_view branch) const;
};

[[nodiscard]] std::string render_relation_projection_text(
    std::string_view branch,
    const std::vector<RelationProjectionEntry>& entries);

}  // namespace pv
