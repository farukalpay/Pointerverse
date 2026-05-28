// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "pv/hash/canonical.hpp"

namespace pv {

class CanonicalHasher {
public:
    void write_u8(std::uint8_t value);
    void write_u32(std::uint32_t value);
    void write_u64(std::uint64_t value);
    void write_i64(std::int64_t value);
    void write_string(std::string_view value);
    void write_f64(double value);
    void write_hash(Hash256 hash);
    void write_bytes(std::span<const std::byte> bytes);

    [[nodiscard]] Hash256 finish() const;

private:
    std::vector<std::byte> bytes_;
};

}  // namespace pv
