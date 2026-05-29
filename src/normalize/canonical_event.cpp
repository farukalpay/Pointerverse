// SPDX-License-Identifier: Apache-2.0
#include "pv/normalize/canonical_event.hpp"

#include <cmath>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "pv/hash/canonical.hpp"
#include "pv/normalize/event_normalizer.hpp"
#include "pv/source/source_event.hpp"

namespace pv {
namespace {

bool valid_key(std::string_view value) noexcept {
    return !value.empty()
        && value.find('\t') == std::string_view::npos
        && value.find('\n') == std::string_view::npos
        && value.find('\r') == std::string_view::npos;
}

std::string string_field(const nlohmann::json& object, const char* name) {
    const auto iter = object.find(name);
    if (iter == object.end() || !iter->is_string()) {
        return {};
    }
    return iter->get<std::string>();
}

nlohmann::json value_to_json(const Value& value) {
    switch (value.kind) {
    case ValueKind::Null:
        return nullptr;
    case ValueKind::Bool:
        return std::get<bool>(value.data);
    case ValueKind::Int64:
        return std::get<std::int64_t>(value.data);
    case ValueKind::UInt64:
        return std::get<std::uint64_t>(value.data);
    case ValueKind::Float64:
        return std::get<double>(value.data);
    case ValueKind::String:
        return std::get<std::string>(value.data);
    case ValueKind::Hash:
        return to_hex(std::get<Hash256>(value.data));
    case ValueKind::ObjectRef:
        return to_string(std::get<ObjectId>(value.data));
    }
    return nullptr;
}

Value value_from_json(const nlohmann::json& value) {
    if (value.is_null()) {
        return null_value();
    }
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
    throw std::invalid_argument("canonical event attributes must be scalar values");
}

std::unordered_map<std::string, Value> attributes_from_json(const nlohmann::json& object) {
    std::unordered_map<std::string, Value> attributes;
    if (object.is_null()) {
        return attributes;
    }
    if (!object.is_object()) {
        throw std::invalid_argument("canonical event attributes must be a JSON object");
    }
    for (const auto& [key, value] : object.items()) {
        attributes.emplace(key, value_from_json(value));
    }
    return attributes;
}

}  // namespace

void validate(const CanonicalEvent& event) {
    if (!valid_key(event.id)) {
        throw std::invalid_argument("canonical event requires a non-empty id without tabs/newlines");
    }
    if (!valid_key(event.source)) {
        throw std::invalid_argument("canonical event requires a non-empty source without tabs/newlines");
    }
    if (event.actor.empty()) {
        throw std::invalid_argument("canonical event requires actor");
    }
    if (event.subject.empty()) {
        throw std::invalid_argument("canonical event requires subject");
    }
    if (event.relation.empty()) {
        throw std::invalid_argument("canonical event requires relation");
    }
}

nlohmann::json to_json(const CanonicalEvent& event) {
    validate(event);
    nlohmann::json attributes = nlohmann::json::object();
    for (const auto& [key, value] : event.attributes) {
        attributes[key] = value_to_json(value);
    }
    return nlohmann::json{
        {"id", event.id},
        {"source", event.source},
        {"kind", event.kind},
        {"actor", event.actor},
        {"subject", event.subject},
        {"relation", event.relation},
        {"observed_at_ms", event.observed_at_ms},
        {"attributes", std::move(attributes)}
    };
}

CanonicalEvent canonical_event_from_json(const nlohmann::json& object) {
    if (!object.is_object()) {
        throw std::invalid_argument("canonical event must be a JSON object");
    }
    CanonicalEvent event;
    event.id = string_field(object, "id");
    event.source = string_field(object, "source");
    event.kind = string_field(object, "kind");
    event.actor = string_field(object, "actor");
    event.subject = string_field(object, "subject");
    event.relation = string_field(object, "relation");
    if (const auto iter = object.find("observed_at_ms"); iter != object.end() && iter->is_number_integer()) {
        event.observed_at_ms = iter->get<std::int64_t>();
    }
    if (const auto iter = object.find("attributes"); iter != object.end()) {
        event.attributes = attributes_from_json(*iter);
    }
    validate(event);
    return event;
}

CanonicalEvent canonical_event_from_jsonl(std::string_view line) {
    return canonical_event_from_json(nlohmann::json::parse(line));
}

std::string to_jsonl(const CanonicalEvent& event) {
    return to_json(event).dump() + "\n";
}

CanonicalEvent SourceEventNormalizer::normalize(const SourceEvent& event) const {
    CanonicalEvent canonical;
    canonical.id = event.id;
    canonical.source = event.source;
    canonical.kind = event.kind;
    canonical.actor = event.actor;
    canonical.subject = event.subject;
    canonical.relation = event.relation;
    canonical.observed_at_ms = event.observed_at_ms;
    canonical.attributes = event.attributes;
    validate(canonical);
    return canonical;
}

}  // namespace pv
