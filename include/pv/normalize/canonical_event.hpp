// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include <nlohmann/json_fwd.hpp>

#include "pv/core/value.hpp"

namespace pv {

struct CanonicalEvent {
    std::string id;
    std::string source;
    std::string kind;
    std::string actor;
    std::string subject;
    std::string relation;
    std::int64_t observed_at_ms{0};
    std::unordered_map<std::string, Value> attributes;
};

void validate(const CanonicalEvent& event);
[[nodiscard]] nlohmann::json to_json(const CanonicalEvent& event);
[[nodiscard]] CanonicalEvent canonical_event_from_json(const nlohmann::json& object);
[[nodiscard]] CanonicalEvent canonical_event_from_jsonl(std::string_view line);
[[nodiscard]] std::string to_jsonl(const CanonicalEvent& event);

}  // namespace pv
