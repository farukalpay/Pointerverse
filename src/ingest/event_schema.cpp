// SPDX-License-Identifier: Apache-2.0
#include "pv/ingest/event_schema.hpp"

#include <cmath>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace pv {

std::string json_scalar_to_string(const nlohmann::json& value) {
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_boolean()) {
        return value.get<bool>() ? "true" : "false";
    }
    if (value.is_number_integer()) {
        return std::to_string(value.get<long long>());
    }
    if (value.is_number_unsigned()) {
        return std::to_string(value.get<unsigned long long>());
    }
    if (value.is_number_float()) {
        const auto number = value.get<double>();
        if (!std::isfinite(number)) {
            throw std::invalid_argument("non-finite numeric value");
        }
        return std::to_string(number);
    }
    return {};
}

std::uint64_t normalize_timestamp_ms(std::uint64_t timestamp) {
    constexpr std::uint64_t seconds_threshold = 1'000'000'000'000ULL;
    return timestamp > 0 && timestamp < seconds_threshold ? timestamp * 1000ULL : timestamp;
}

std::uint64_t timestamp_ms_from_json(const nlohmann::json& object) {
    if (!object.is_object()) {
        return 0;
    }
    if (const auto iter = object.find("ts_ms"); iter != object.end() && iter->is_number_unsigned()) {
        return iter->get<std::uint64_t>();
    }
    if (const auto iter = object.find("timestamp_ms"); iter != object.end() && iter->is_number_unsigned()) {
        return iter->get<std::uint64_t>();
    }
    if (const auto iter = object.find("ts"); iter != object.end() && iter->is_number_unsigned()) {
        return normalize_timestamp_ms(iter->get<std::uint64_t>());
    }
    if (const auto iter = object.find("timestamp"); iter != object.end() && iter->is_number_unsigned()) {
        return normalize_timestamp_ms(iter->get<std::uint64_t>());
    }
    return 0;
}

}  // namespace pv
