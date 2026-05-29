// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include "pv/core/value.hpp"

namespace pv {

struct SourceEvent {
    std::string id;
    std::string source;
    std::string kind;
    std::string actor;
    std::string subject;
    std::string relation;
    std::int64_t observed_at_ms{0};
    std::unordered_map<std::string, Value> attributes;
};

[[nodiscard]] bool valid_source_event_key(std::string_view value) noexcept;

}  // namespace pv
