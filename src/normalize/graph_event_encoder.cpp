// SPDX-License-Identifier: Apache-2.0
#include "pv/normalize/graph_event_encoder.hpp"

#include <cmath>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "pv/hash/canonical.hpp"

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

std::string string_attribute(const std::unordered_map<std::string, Value>& attributes, const char* name) {
    const auto iter = attributes.find(name);
    if (iter == attributes.end() || iter->second.kind != ValueKind::String) {
        return {};
    }
    return std::get<std::string>(iter->second.data);
}

double numeric_attribute(
    const std::unordered_map<std::string, Value>& attributes,
    const char* name,
    double fallback) {
    const auto iter = attributes.find(name);
    if (iter == attributes.end()) {
        return fallback;
    }
    const auto& value = iter->second;
    if (value.kind == ValueKind::Float64) {
        return std::get<double>(value.data);
    }
    if (value.kind == ValueKind::UInt64) {
        return static_cast<double>(std::get<std::uint64_t>(value.data));
    }
    if (value.kind == ValueKind::Int64) {
        return static_cast<double>(std::get<std::int64_t>(value.data));
    }
    return fallback;
}

bool encoder_reserved(std::string_view key) {
    return key == "actor_type" || key == "subject_type" || key == "weight" || key == "role";
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
    throw std::invalid_argument("graph event attributes must be scalar values");
}

std::unordered_map<std::string, Value> attributes_from_json(const nlohmann::json& object) {
    std::unordered_map<std::string, Value> attributes;
    if (object.is_null()) {
        return attributes;
    }
    if (!object.is_object()) {
        throw std::invalid_argument("graph event attributes must be a JSON object");
    }
    for (const auto& [key, value] : object.items()) {
        attributes.emplace(key, value_from_json(value));
    }
    return attributes;
}

}  // namespace

void validate(const GraphEvent& event) {
    if (!valid_key(event.id)) {
        throw std::invalid_argument("graph event requires a non-empty id without tabs/newlines");
    }
    if (!valid_key(event.source)) {
        throw std::invalid_argument("graph event requires a non-empty source without tabs/newlines");
    }
    if (event.from.empty() || event.to.empty() || event.relation.empty()) {
        throw std::invalid_argument("graph event requires from, to, and relation");
    }
    if (!std::isfinite(event.weight)) {
        throw std::invalid_argument("graph event weight must be finite");
    }
}

nlohmann::json to_json(const GraphEvent& event) {
    validate(event);
    nlohmann::json attributes = nlohmann::json::object();
    for (const auto& [key, value] : event.attributes) {
        attributes[key] = value_to_json(value);
    }
    return nlohmann::json{
        {"id", event.id},
        {"source", event.source},
        {"from", event.from},
        {"from_type", event.from_type},
        {"to", event.to},
        {"to_type", event.to_type},
        {"relation", event.relation},
        {"weight", event.weight},
        {"role", event.role},
        {"attributes", std::move(attributes)}
    };
}

GraphEvent graph_event_from_json(const nlohmann::json& object) {
    if (!object.is_object()) {
        throw std::invalid_argument("graph event must be a JSON object");
    }
    GraphEvent event;
    event.id = string_field(object, "id");
    event.source = string_field(object, "source");
    event.from = string_field(object, "from");
    event.from_type = string_field(object, "from_type");
    event.to = string_field(object, "to");
    event.to_type = string_field(object, "to_type");
    event.relation = string_field(object, "relation");
    event.role = string_field(object, "role");
    if (event.source.empty()) {
        event.source = "graph-log";
    }
    if (event.from_type.empty()) {
        event.from_type = "Entity";
    }
    if (event.to_type.empty()) {
        event.to_type = "Entity";
    }
    if (event.role.empty()) {
        event.role = "Structural";
    }
    if (const auto iter = object.find("weight"); iter != object.end() && iter->is_number()) {
        event.weight = iter->get<double>();
    }
    if (const auto iter = object.find("attributes"); iter != object.end()) {
        event.attributes = attributes_from_json(*iter);
    }
    validate(event);
    return event;
}

GraphEvent graph_event_from_jsonl(std::string_view line) {
    return graph_event_from_json(nlohmann::json::parse(line));
}

std::string to_jsonl(const GraphEvent& event) {
    return to_json(event).dump() + "\n";
}

GraphEvent GraphEventEncoder::encode(const CanonicalEvent& event) const {
    validate(event);

    GraphEvent graph;
    graph.id = event.id;
    graph.source = event.source;
    graph.from = event.actor;
    graph.from_type = string_attribute(event.attributes, "actor_type");
    graph.to = event.subject;
    graph.to_type = string_attribute(event.attributes, "subject_type");
    graph.relation = event.relation;
    graph.weight = numeric_attribute(event.attributes, "weight", 1.0);
    graph.role = string_attribute(event.attributes, "role");
    if (graph.from_type.empty()) {
        graph.from_type = "Entity";
    }
    if (graph.to_type.empty()) {
        graph.to_type = "Entity";
    }
    if (graph.role.empty()) {
        graph.role = "Structural";
    }

    for (const auto& [key, value] : event.attributes) {
        if (!encoder_reserved(key)) {
            graph.attributes.emplace(key, value);
        }
    }
    graph.attributes.emplace("canonical_source", string_value(event.source));
    graph.attributes.emplace("canonical_kind", string_value(event.kind));
    graph.attributes.emplace("observed_at_ms", int64_value(event.observed_at_ms));
    validate(graph);
    return graph;
}

}  // namespace pv
