// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json_fwd.hpp>

namespace pv {

[[nodiscard]] std::string json_scalar_to_string(const nlohmann::json& value);
[[nodiscard]] std::uint64_t timestamp_ms_from_json(const nlohmann::json& object);
[[nodiscard]] std::uint64_t normalize_timestamp_ms(std::uint64_t timestamp);

}  // namespace pv
