// SPDX-License-Identifier: Apache-2.0
#include "pv/hash/canonical.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "pv/core/delta.hpp"
#include "pv/core/snapshot.hpp"
#include "pv/hash/hasher.hpp"
#include "pv/law/law.hpp"
#include "pv/kernel/canonical_codec.hpp"
#include "pv/trace/event.hpp"

namespace pv {
namespace {

int hex_value(char ch) noexcept {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

std::string strip_hex_prefix(std::string_view text) {
    if (text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text.remove_prefix(2);
    }
    return std::string{text};
}

}  // namespace

bool empty(Hash256 hash) noexcept {
    return std::ranges::all_of(hash.value, [](std::byte byte) {
        return byte == std::byte{0};
    });
}

std::string to_hex(Hash256 hash) {
    static constexpr auto digits = std::string_view{"0123456789abcdef"};
    std::string out;
    out.reserve(hash.value.size() * 2);
    for (const auto byte : hash.value) {
        const auto value = static_cast<unsigned char>(byte);
        out.push_back(digits[value >> 4U]);
        out.push_back(digits[value & 0x0fU]);
    }
    return out;
}

std::optional<Hash256> parse_hash256(std::string_view text) {
    const auto stripped = strip_hex_prefix(text);
    if (stripped.size() != 64) {
        return std::nullopt;
    }

    Hash256 out;
    for (std::size_t index = 0; index < out.value.size(); ++index) {
        const auto high = hex_value(stripped[index * 2]);
        const auto low = hex_value(stripped[index * 2 + 1]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        out.value[index] = static_cast<std::byte>((high << 4) | low);
    }
    return out;
}

std::uint64_t truncated_u64(Hash256 hash) noexcept {
    std::uint64_t out = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        out = (out << 8U) | static_cast<unsigned char>(hash.value[index]);
    }
    return out;
}

std::uint64_t canonical_f64(double value) noexcept {
    if (std::isnan(value)) {
        return 0x7ff8000000000000ULL;
    }
    if (value == 0.0) {
        return 0;
    }

    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

Hash256 canonical_hash(const WorldSnapshot& snapshot) {
    return sha256(canonical_encode(snapshot));
}

Hash256 canonical_hash(const Delta& delta) {
    return sha256(canonical_encode(delta));
}

Hash256 canonical_hash(const TraceEvent& event) {
    return sha256(canonical_encode(event));
}

Hash256 canonical_hash(const std::vector<TraceEvent>& events) {
    return sha256(canonical_encode(events));
}

Hash256 canonical_hash(const std::vector<LawStatus>& statuses) {
    return sha256(canonical_encode(statuses));
}

Hash256 canonical_hash(const std::vector<LawViolation>& violations) {
    return sha256(canonical_encode(violations));
}

Hash256 canonical_hash_morphism_path(const std::vector<std::string>& path) {
    return sha256(canonical_encode_morphism_path(path));
}

}  // namespace pv
