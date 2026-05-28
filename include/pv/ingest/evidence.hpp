// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pv {

struct EvidenceEvent {
    std::string source;
    std::string event_id;
    std::string actor;
    std::string action;
    std::string target;
    std::string target_type;
    std::uint64_t timestamp_ms{0};
    std::vector<std::pair<std::string, std::string>> attributes;
};

struct NormalizedAuditEvent {
    std::string from;
    std::string from_type;
    std::string relation;
    std::string to;
    std::string to_type;
    std::string actor;
    std::string evidence_id;
    std::string source;
    std::string action;
    std::uint64_t timestamp_ms{0};
};

[[nodiscard]] std::optional<std::string> attribute_value(
    const EvidenceEvent& event,
    std::string_view name);
[[nodiscard]] std::string evidence_object_name(const EvidenceEvent& event);
[[nodiscard]] bool valid_evidence_key(std::string_view value) noexcept;

}  // namespace pv
