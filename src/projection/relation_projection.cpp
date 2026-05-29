// SPDX-License-Identifier: Apache-2.0
#include "pv/projection/relation_projection.hpp"

#include <fmt/format.h>

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

std::vector<RelationProjectionEntry> RelationProjection::project(
    const ProjectionStore& store,
    std::string_view branch) const {
    const auto snapshot = store.snapshot(branch);
    std::map<std::string, RelationProjectionEntry> entries;
    std::map<std::string, std::set<std::string>> evidence;

    for (const auto& pointer : snapshot.pointers) {
        const auto relation = snapshot.relation_name(pointer.relation);
        auto& entry = entries[relation];
        entry.relation = relation;
        entry.occurrences += 1;
    }

    for (const auto& record : store.history(branch)) {
        for (const auto& event : record.events) {
            const auto relation = field_or(event, "relation");
            if (relation.empty()) {
                continue;
            }
            auto& entry = entries[relation];
            entry.relation = relation;
            if (const auto id = evidence_id(event); !id.empty()) {
                evidence[relation].insert(id);
            }
        }
    }

    std::vector<RelationProjectionEntry> out;
    out.reserve(entries.size());
    for (auto& [relation, entry] : entries) {
        if (const auto iter = evidence.find(relation); iter != evidence.end()) {
            entry.evidence_event_ids.assign(iter->second.begin(), iter->second.end());
        }
        out.push_back(std::move(entry));
    }
    return out;
}

std::string render_relation_projection_text(
    std::string_view branch,
    const std::vector<RelationProjectionEntry>& entries) {
    std::ostringstream output;
    output << fmt::format("Relation projection: {}\n", branch);
    output << "--------------------\n";
    if (entries.empty()) {
        output << "empty\n";
        return output.str();
    }
    for (const auto& entry : entries) {
        output << fmt::format(
            "{} occurrences={} evidence={}\n",
            entry.relation,
            entry.occurrences,
            entry.evidence_event_ids.size());
    }
    return output.str();
}

}  // namespace pv
