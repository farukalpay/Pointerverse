// SPDX-License-Identifier: Apache-2.0
#include "pv/ingest/evidence.hpp"

namespace pv {

std::optional<std::string> attribute_value(const EvidenceEvent& event, std::string_view name) {
    for (const auto& [key, value] : event.attributes) {
        if (key == name) {
            return value;
        }
    }
    return std::nullopt;
}

std::string evidence_object_name(const EvidenceEvent& event) {
    return "Evidence/" + event.source + "/" + event.event_id;
}

bool valid_evidence_key(std::string_view value) noexcept {
    return !value.empty()
        && value.find('\t') == std::string_view::npos
        && value.find('\n') == std::string_view::npos
        && value.find('\r') == std::string_view::npos;
}

}  // namespace pv
