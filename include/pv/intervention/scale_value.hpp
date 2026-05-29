// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pv/hash/canonical.hpp"

namespace pv {

class CanonicalWriter;

struct ScaleValue {
    std::uint64_t numerator{0};
    std::uint8_t exponent{0};

    [[nodiscard]] static ScaleValue zero() noexcept;
    [[nodiscard]] static ScaleValue one() noexcept;
    [[nodiscard]] static ScaleValue dyadic(std::uint64_t numerator, std::uint8_t exponent);

    [[nodiscard]] std::uint64_t denominator() const noexcept;
    [[nodiscard]] double to_double() const noexcept;
    [[nodiscard]] std::string string() const;

    friend bool operator==(const ScaleValue&, const ScaleValue&) = default;
};

[[nodiscard]] std::optional<ScaleValue> parse_scale_value(std::string_view text);
[[nodiscard]] std::string to_string(ScaleValue value);
[[nodiscard]] Hash256 scale_value_hash(ScaleValue value);
[[nodiscard]] std::vector<ScaleValue> dyadic_refinement_scales(std::uint8_t depth);

[[nodiscard]] bool operator<(ScaleValue left, ScaleValue right) noexcept;
[[nodiscard]] bool operator>(ScaleValue left, ScaleValue right) noexcept;
[[nodiscard]] bool operator<=(ScaleValue left, ScaleValue right) noexcept;
[[nodiscard]] bool operator>=(ScaleValue left, ScaleValue right) noexcept;

void encode(CanonicalWriter& writer, ScaleValue value);

}  // namespace pv
