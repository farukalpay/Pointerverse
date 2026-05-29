// SPDX-License-Identifier: Apache-2.0
#include "pv/projection/timeline_projection.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <sstream>

#include "pv/hash/canonical.hpp"
#include "pv/projection/projection_store.hpp"
#include "pv/runtime/transaction_types.hpp"

namespace pv {
namespace {

std::string short_hash(CommitId id) {
    return to_hex(id.value).substr(0, 12);
}

std::string field_or(const TraceEvent& event, std::string_view name, std::string fallback = "?") {
    const auto iter = event.fields.find(std::string{name});
    return iter == event.fields.end() ? std::move(fallback) : iter->second;
}

std::string evidence_id(const TraceEvent& event) {
    const auto source = field_or(event, "source", "");
    const auto id = field_or(event, "id", field_or(event, "event_id", ""));
    if (source.empty() || id.empty()) {
        return {};
    }
    return source + "/" + id;
}

std::string detail_for(const TraceEvent& event) {
    if (event.event == "graph.event") {
        return fmt::format(
            "{} -> {} : {}",
            field_or(event, "from"),
            field_or(event, "to"),
            field_or(event, "relation"));
    }
    if (event.event == "evidence.ingest") {
        return fmt::format(
            "{} {} {}",
            field_or(event, "source"),
            field_or(event, "event_id"),
            field_or(event, "action"));
    }
    return event.event;
}

}  // namespace

std::vector<TimelineEntry> TimelineProjection::project(
    const ProjectionStore& store,
    std::string_view branch) const {
    std::vector<TimelineEntry> entries;
    for (const auto& record : store.history(branch)) {
        if (record.origin == TransactionOrigin::Internal) {
            continue;
        }
        for (const auto& event : record.events) {
            entries.push_back(TimelineEntry{
                record.id,
                event.epoch,
                event.event,
                detail_for(event),
                evidence_id(event)
            });
        }
    }
    std::ranges::sort(entries, [](const auto& left, const auto& right) {
        if (left.epoch.value != right.epoch.value) {
            return left.epoch.value < right.epoch.value;
        }
        if (left.event != right.event) {
            return left.event < right.event;
        }
        return left.detail < right.detail;
    });
    return entries;
}

std::string render_timeline_projection_text(
    std::string_view branch,
    const std::vector<TimelineEntry>& entries) {
    std::ostringstream output;
    output << fmt::format("Timeline projection: {}\n", branch);
    output << "--------------------\n";
    if (entries.empty()) {
        output << "empty\n";
        return output.str();
    }
    for (const auto& entry : entries) {
        output << fmt::format(
            "epoch {} commit {} {} {}\n",
            entry.epoch.value,
            short_hash(entry.commit),
            entry.event,
            entry.detail);
    }
    return output.str();
}

}  // namespace pv
