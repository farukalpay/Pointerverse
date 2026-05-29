// SPDX-License-Identifier: Apache-2.0
#include "pv/projection/entity_projection.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <map>
#include <set>
#include <sstream>

#include "pv/projection/projection_store.hpp"

namespace pv {
namespace {

std::string field_or(const TraceEvent& event, std::string_view name, std::string fallback = "") {
    const auto iter = event.fields.find(std::string{name});
    return iter == event.fields.end() ? std::move(fallback) : iter->second;
}

std::string evidence_id(const TraceEvent& event) {
    const auto source = field_or(event, "source");
    const auto id = field_or(event, "id", field_or(event, "event_id"));
    if (source.empty() || id.empty()) {
        return {};
    }
    return source + "/" + id;
}

}  // namespace

std::vector<EntityProjectionEntry> EntityProjection::project(
    const ProjectionStore& store,
    std::string_view branch) const {
    const auto snapshot = store.snapshot(branch);
    std::map<std::string, EntityProjectionEntry> entries;
    std::map<std::string, std::set<std::string>> evidence;

    for (const auto& object : snapshot.objects) {
        auto& entry = entries[object.name];
        entry.entity = object.name;
        entry.type = snapshot.type_name(object.type);
        entry.incoming = object.incoming_count;
        entry.outgoing = object.outgoing_count;
    }

    for (const auto& record : store.history(branch)) {
        for (const auto& event : record.events) {
            const auto id = evidence_id(event);
            for (const auto* field : {"from", "to", "actor", "subject"}) {
                const auto entity = field_or(event, field);
                if (entity.empty()) {
                    continue;
                }
                auto& entry = entries[entity];
                entry.entity = entity;
                entry.appearances += 1;
                if (!id.empty()) {
                    evidence[entity].insert(id);
                }
            }
        }
    }

    std::vector<EntityProjectionEntry> out;
    out.reserve(entries.size());
    for (auto& [entity, entry] : entries) {
        if (const auto iter = evidence.find(entity); iter != evidence.end()) {
            entry.evidence_event_ids.assign(iter->second.begin(), iter->second.end());
        }
        out.push_back(std::move(entry));
    }
    return out;
}

std::string render_entity_projection_text(
    std::string_view branch,
    const std::vector<EntityProjectionEntry>& entries) {
    std::ostringstream output;
    output << fmt::format("Entity projection: {}\n", branch);
    output << "------------------\n";
    if (entries.empty()) {
        output << "empty\n";
        return output.str();
    }
    for (const auto& entry : entries) {
        output << fmt::format(
            "{} type={} appearances={} incoming={} outgoing={} evidence={}\n",
            entry.entity,
            entry.type.empty() ? "?" : entry.type,
            entry.appearances,
            entry.incoming,
            entry.outgoing,
            entry.evidence_event_ids.size());
    }
    return output.str();
}

}  // namespace pv
