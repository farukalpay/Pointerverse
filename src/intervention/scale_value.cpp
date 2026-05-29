// SPDX-License-Identifier: Apache-2.0
#include "pv/intervention/scale_value.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <stdexcept>

#include "pv/hash/hasher.hpp"
#include "pv/kernel/canonical_codec.hpp"

namespace pv {
namespace {

constexpr std::uint8_t max_exponent = 62;

ScaleValue normalize(std::uint64_t numerator, std::uint8_t exponent) {
    if (exponent > max_exponent) {
        throw std::invalid_argument("dyadic scale exponent is too large");
    }
    const auto denominator = std::uint64_t{1} << exponent;
    if (numerator > denominator) {
        throw std::invalid_argument("scale must be in [0, 1]");
    }
    if (numerator == 0) {
        return ScaleValue{0, 0};
    }
    while (exponent > 0 && (numerator % 2U) == 0U) {
        numerator /= 2U;
        exponent -= 1U;
    }
    return ScaleValue{numerator, exponent};
}

std::optional<std::uint64_t> parse_u64(std::string_view text) {
    std::uint64_t value = 0;
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return value;
}

bool is_power_of_two(std::uint64_t value) noexcept {
    return value != 0 && (value & (value - 1U)) == 0U;
}

std::uint8_t exponent_for_denominator(std::uint64_t denominator) {
    std::uint8_t exponent = 0;
    while (denominator > 1) {
        denominator >>= 1U;
        exponent += 1U;
    }
    return exponent;
}

}  // namespace

ScaleValue ScaleValue::zero() noexcept {
    return ScaleValue{0, 0};
}

ScaleValue ScaleValue::one() noexcept {
    return ScaleValue{1, 0};
}

ScaleValue ScaleValue::dyadic(std::uint64_t numerator, std::uint8_t exponent) {
    return normalize(numerator, exponent);
}

std::uint64_t ScaleValue::denominator() const noexcept {
    return std::uint64_t{1} << exponent;
}

double ScaleValue::to_double() const noexcept {
    return static_cast<double>(numerator) / static_cast<double>(denominator());
}

std::string ScaleValue::string() const {
    if (exponent == 0) {
        return std::to_string(numerator);
    }
    return std::to_string(numerator) + "/" + std::to_string(denominator());
}

std::optional<ScaleValue> parse_scale_value(std::string_view text) {
    const auto slash = text.find('/');
    if (slash == std::string_view::npos) {
        auto numerator = parse_u64(text);
        if (!numerator.has_value() || *numerator > 1) {
            return std::nullopt;
        }
        return ScaleValue::dyadic(*numerator, 0);
    }
    auto numerator = parse_u64(text.substr(0, slash));
    auto denominator = parse_u64(text.substr(slash + 1));
    if (!numerator.has_value()
        || !denominator.has_value()
        || !is_power_of_two(*denominator)) {
        return std::nullopt;
    }
    const auto exponent = exponent_for_denominator(*denominator);
    if (exponent > max_exponent) {
        return std::nullopt;
    }
    try {
        return ScaleValue::dyadic(*numerator, exponent);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::string to_string(ScaleValue value) {
    return value.string();
}

void encode(CanonicalWriter& writer, ScaleValue value) {
    value = ScaleValue::dyadic(value.numerator, value.exponent);
    writer.string("ScaleValue:v1");
    writer.u64(value.numerator);
    writer.u8(value.exponent);
}

Hash256 scale_value_hash(ScaleValue value) {
    CanonicalWriter writer;
    encode(writer, value);
    return sha256(writer.bytes());
}

std::vector<ScaleValue> dyadic_refinement_scales(std::uint8_t depth) {
    if (depth > max_exponent) {
        throw std::invalid_argument("dyadic refinement depth is too large");
    }
    const auto count = std::uint64_t{1} << depth;
    std::vector<ScaleValue> scales;
    scales.reserve(static_cast<std::size_t>(count + 1U));
    for (std::uint64_t numerator = 0; numerator <= count; ++numerator) {
        scales.push_back(ScaleValue::dyadic(numerator, depth));
    }
    std::ranges::sort(scales);
    scales.erase(std::ranges::unique(scales).begin(), scales.end());
    return scales;
}

bool operator<(ScaleValue left, ScaleValue right) noexcept {
    const auto lhs = static_cast<unsigned __int128>(left.numerator) << right.exponent;
    const auto rhs = static_cast<unsigned __int128>(right.numerator) << left.exponent;
    return lhs < rhs;
}

bool operator>(ScaleValue left, ScaleValue right) noexcept {
    return right < left;
}

bool operator<=(ScaleValue left, ScaleValue right) noexcept {
    return !(right < left);
}

bool operator>=(ScaleValue left, ScaleValue right) noexcept {
    return !(left < right);
}

}  // namespace pv
