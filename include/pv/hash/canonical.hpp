// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pv {

struct Delta;
struct LawStatus;
struct LawViolation;
struct TraceEvent;
struct WorldSnapshot;

struct Hash256 {
    std::array<std::byte, 32> value{};

    friend bool operator==(const Hash256&, const Hash256&) = default;
};

[[nodiscard]] bool empty(Hash256 hash) noexcept;
[[nodiscard]] std::string to_hex(Hash256 hash);
[[nodiscard]] std::optional<Hash256> parse_hash256(std::string_view text);
[[nodiscard]] std::uint64_t truncated_u64(Hash256 hash) noexcept;
[[nodiscard]] std::uint64_t canonical_f64(double value) noexcept;

[[nodiscard]] Hash256 canonical_hash(const WorldSnapshot& snapshot);
[[nodiscard]] Hash256 canonical_hash(const Delta& delta);
[[nodiscard]] Hash256 canonical_hash(const TraceEvent& event);
[[nodiscard]] Hash256 canonical_hash(const std::vector<TraceEvent>& events);
[[nodiscard]] Hash256 canonical_hash(const std::vector<LawStatus>& statuses);
[[nodiscard]] Hash256 canonical_hash(const std::vector<LawViolation>& violations);
[[nodiscard]] Hash256 canonical_hash_morphism_path(const std::vector<std::string>& path);

}  // namespace pv
