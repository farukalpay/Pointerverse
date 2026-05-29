// SPDX-License-Identifier: Apache-2.0
#include "pv/source/source_adapter.hpp"

#include <cmath>
#include <istream>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

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

std::int64_t normalize_timestamp_ms(std::int64_t timestamp) {
    constexpr std::int64_t seconds_threshold = 1'000'000'000'000LL;
    return timestamp > 0 && timestamp < seconds_threshold ? timestamp * 1000LL : timestamp;
}

std::int64_t timestamp_ms_from_json(const nlohmann::json& object) {
    for (const auto* name : {"ts_ms", "timestamp_ms"}) {
        const auto iter = object.find(name);
        if (iter != object.end() && iter->is_number_integer()) {
            return iter->get<std::int64_t>();
        }
    }
    for (const auto* name : {"ts", "timestamp"}) {
        const auto iter = object.find(name);
        if (iter != object.end() && iter->is_number_integer()) {
            return normalize_timestamp_ms(iter->get<std::int64_t>());
        }
    }
    return 0;
}

Value value_from_json(const nlohmann::json& value) {
    if (value.is_boolean()) {
        return bool_value(value.get<bool>());
    }
    if (value.is_number_unsigned()) {
        return uint64_value(value.get<std::uint64_t>());
    }
    if (value.is_number_integer()) {
        return int64_value(value.get<std::int64_t>());
    }
    if (value.is_number_float()) {
        const auto number = value.get<double>();
        if (!std::isfinite(number)) {
            throw std::invalid_argument("non-finite numeric value");
        }
        return float64_value(number);
    }
    if (value.is_string()) {
        return string_value(value.get<std::string>());
    }
    return null_value();
}

bool is_reserved(std::string_view key) {
    for (const auto* reserved : {
             "id", "event_id", "source", "kind", "event", "action",
             "actor", "agent", "user", "tool", "from", "source_object",
             "subject", "target", "to", "target_object", "path", "file",
             "resource", "repo", "repository", "pr", "pull_request",
             "relation", "rel", "ts", "ts_ms", "timestamp", "timestamp_ms"}) {
        if (key == reserved) {
            return true;
        }
    }
    return false;
}

SourceEvent parse_event(
    const nlohmann::json& object,
    const std::string& default_source,
    SourceBatch& batch,
    std::size_t line_number) {
    if (!object.is_object()) {
        throw std::invalid_argument("source event line must be a JSON object");
    }

    SourceEvent event;
    event.id = string_field(object, {"id", "event_id"});
    event.source = string_field(object, {"source"});
    if (event.source.empty()) {
        event.source = default_source;
    }
    event.kind = string_field(object, {"kind", "event", "action"});
    event.actor = string_field(object, {"actor", "agent", "user", "tool", "from", "source_object"});
    event.subject = string_field(object, {
        "subject", "target", "to", "target_object", "path", "file",
        "resource", "repo", "repository", "pr", "pull_request"});
    event.relation = string_field(object, {"relation", "rel", "action", "event"});
    event.observed_at_ms = timestamp_ms_from_json(object);

    if (event.id.empty()) {
        throw std::invalid_argument("source event requires id or event_id");
    }

    for (const auto& [key, value] : object.items()) {
        if (value.is_null() || is_reserved(key)) {
            continue;
        }
        if (value.is_object() || value.is_array()) {
            batch.errors.push_back(SourceError{
                line_number,
                event.id,
                "nested field '" + key + "' ignored"});
            continue;
        }
        event.attributes.emplace(key, value_from_json(value));
    }

    return event;
}

}  // namespace

JsonlSourceAdapter::JsonlSourceAdapter(std::string default_source)
    : default_source_(std::move(default_source)) {}

SourceBatch JsonlSourceAdapter::read(std::istream& input) const {
    SourceBatch batch;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        line_number += 1;
        if (line.empty()) {
            continue;
        }

        try {
            const auto json = nlohmann::json::parse(line);
            batch.events.push_back(parse_event(json, default_source_, batch, line_number));
        } catch (const std::exception& error) {
            batch.errors.push_back(SourceError{line_number, {}, error.what()});
        }
    }
    return batch;
}

}  // namespace pv
