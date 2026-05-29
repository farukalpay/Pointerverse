// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace pv {

class ProjectionStore;

struct EntityProjectionEntry {
    std::string entity;
    std::string type;
    std::size_t appearances{0};
    std::size_t incoming{0};
    std::size_t outgoing{0};
    std::vector<std::string> evidence_event_ids;

    friend bool operator==(const EntityProjectionEntry&, const EntityProjectionEntry&) = default;
};

class EntityProjection {
public:
    [[nodiscard]] std::vector<EntityProjectionEntry> project(
        const ProjectionStore& store,
        std::string_view branch) const;
};

[[nodiscard]] std::string render_entity_projection_text(
    std::string_view branch,
    const std::vector<EntityProjectionEntry>& entries);

}  // namespace pv
