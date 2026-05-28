// SPDX-License-Identifier: Apache-2.0
#include "pv/ingest/jsonl_adapter.hpp"

#include <istream>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

#include "pv/ingest/event_schema.hpp"

namespace pv {
namespace {

std::string string_field(const nlohmann::json& object, std::initializer_list<const char*> names) {
    for (const auto* name : names) {
        const auto iter = object.find(name);
        if (iter != object.end() && iter->is_string()) {
            return iter->get<std::string>();
        }
    }
    return {};
}

std::string target_field(const nlohmann::json& object) {
    return string_field(object, {"target", "path", "file", "pr", "pull_request", "test", "test_run", "secret", "resource", "repo", "repository"});
}

std::vector<std::pair<std::string, std::string>> attributes_from_json(const nlohmann::json& object) {
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& [key, value] : object.items()) {
        if (value.is_null() || value.is_object() || value.is_array()) {
            continue;
        }
        out.emplace_back(key, json_scalar_to_string(value));
    }
    return out;
}

EvidenceEvent parse_event(const nlohmann::json& object, const std::string& default_source) {
    if (!object.is_object()) {
        throw std::invalid_argument("JSONL evidence line must be an object");
    }

    EvidenceEvent event;
    event.source = string_field(object, {"source"});
    if (event.source.empty()) {
        event.source = default_source;
    }
    event.event_id = string_field(object, {"event_id", "id"});
    event.actor = string_field(object, {"actor", "agent", "user", "tool"});
    event.action = string_field(object, {"event", "action"});
    event.target = target_field(object);
    event.target_type = string_field(object, {"target_type", "object_type", "type"});
    event.timestamp_ms = timestamp_ms_from_json(object);
    event.attributes = attributes_from_json(object);

    if (event.event_id.empty()) {
        throw std::invalid_argument("evidence event requires id or event_id");
    }
    if (event.action.empty()) {
        throw std::invalid_argument("evidence event requires event or action");
    }
    return event;
}

}  // namespace

JsonlEvidenceAdapter::JsonlEvidenceAdapter(std::string default_source)
    : default_source_(std::move(default_source)) {}

EvidenceBatch JsonlEvidenceAdapter::read(std::istream& input) const {
    EvidenceBatch batch;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        line_number += 1;
        if (line.empty()) {
            continue;
        }

        try {
            const auto json = nlohmann::json::parse(line);
            batch.events.push_back(parse_event(json, default_source_));
        } catch (const std::exception& error) {
            batch.errors.push_back(EvidenceReadError{line_number, error.what()});
        }
    }
    return batch;
}

}  // namespace pv
