// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "pv/core/id.hpp"
#include "pv/runtime/ids.hpp"

namespace pv {

class ProjectionStore;

struct TimelineEntry {
    CommitId commit;
    Epoch epoch;
    std::string event;
    std::string detail;
    std::string evidence_event_id;

    friend bool operator==(const TimelineEntry&, const TimelineEntry&) = default;
};

class TimelineProjection {
public:
    [[nodiscard]] std::vector<TimelineEntry> project(
        const ProjectionStore& store,
        std::string_view branch) const;
};

[[nodiscard]] std::string render_timeline_projection_text(
    std::string_view branch,
    const std::vector<TimelineEntry>& entries);

}  // namespace pv
