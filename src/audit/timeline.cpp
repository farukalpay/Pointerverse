// SPDX-License-Identifier: Apache-2.0
#include "pv/audit/timeline.hpp"

#include <fmt/format.h>

#include <sstream>

#include "pv/hash/canonical.hpp"
#include "pv/runtime/transaction.hpp"
#include "pv/storage/repository.hpp"

namespace pv {
namespace {

std::string short_hash(CommitId id) {
    return to_hex(id.value).substr(0, 12);
}

bool field_matches(const TraceEvent& event, std::string_view object_name) {
    for (const auto* key : {"object", "from", "to", "actor"}) {
        const auto iter = event.fields.find(key);
        if (iter != event.fields.end() && iter->second == object_name) {
            return true;
        }
    }
    return false;
}

std::string detail_for(const TraceEvent& event) {
    if (event.event == "pointer.create") {
        return fmt::format(
            "{} -> {} : {}",
            event.fields.contains("from") ? event.fields.at("from") : "?",
            event.fields.contains("to") ? event.fields.at("to") : "?",
            event.fields.contains("relation") ? event.fields.at("relation") : "?");
    }
    if (event.event == "object.create") {
        return fmt::format(
            "{} : {}",
            event.fields.contains("object") ? event.fields.at("object") : "?",
            event.fields.contains("type") ? event.fields.at("type") : "?");
    }
    if (event.event == "evidence.ingest") {
        return fmt::format(
            "{} {} {}",
            event.fields.contains("source") ? event.fields.at("source") : "?",
            event.fields.contains("event_id") ? event.fields.at("event_id") : "?",
            event.fields.contains("action") ? event.fields.at("action") : "?");
    }
    return event.event;
}

}  // namespace

std::vector<AuditTimelineEntry> audit_timeline(
    const Repository& repository,
    std::string_view branch,
    std::string_view object_name) {
    std::vector<AuditTimelineEntry> out;
    for (const auto& record : repository.history(branch)) {
        if (record.origin == TransactionOrigin::Internal) {
            continue;
        }
        for (const auto& event : record.events) {
            if (!field_matches(event, object_name)) {
                continue;
            }
            out.push_back(AuditTimelineEntry{
                record.id,
                event.epoch,
                event.event,
                detail_for(event)
            });
        }
    }
    return out;
}

std::string render_audit_timeline_text(
    std::string_view branch,
    std::string_view object_name,
    const std::vector<AuditTimelineEntry>& entries) {
    std::ostringstream output;
    output << fmt::format("Audit timeline: {} {}\n", branch, object_name);
    output << "----------------\n";
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
