// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

#include <nlohmann/json_fwd.hpp>

#include "pv/core/value.hpp"
#include "pv/normalize/canonical_event.hpp"

namespace pv {

struct GraphEvent {
    std::string id;
    std::string source;
    std::string from;
    std::string from_type;
    std::string to;
    std::string to_type;
    std::string relation;
    double weight{1.0};
    std::string role{"Structural"};
    std::unordered_map<std::string, Value> attributes;
};

void validate(const GraphEvent& event);
[[nodiscard]] nlohmann::json to_json(const GraphEvent& event);
[[nodiscard]] GraphEvent graph_event_from_json(const nlohmann::json& object);
[[nodiscard]] GraphEvent graph_event_from_jsonl(std::string_view line);
[[nodiscard]] std::string to_jsonl(const GraphEvent& event);

class GraphEventEncoder {
public:
    [[nodiscard]] GraphEvent encode(const CanonicalEvent& event) const;
};

}  // namespace pv
