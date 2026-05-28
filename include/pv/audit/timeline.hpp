// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "pv/core/id.hpp"
#include "pv/runtime/ids.hpp"

namespace pv {

class Repository;

struct AuditTimelineEntry {
    CommitId commit;
    Epoch epoch;
    std::string event;
    std::string detail;
};

[[nodiscard]] std::vector<AuditTimelineEntry> audit_timeline(
    const Repository& repository,
    std::string_view branch,
    std::string_view object_name);
[[nodiscard]] std::string render_audit_timeline_text(
    std::string_view branch,
    std::string_view object_name,
    const std::vector<AuditTimelineEntry>& entries);

}  // namespace pv
