// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <variant>

#include "pv/core/id.hpp"
#include "pv/hash/canonical.hpp"

namespace pv {

enum class ValueKind : std::uint8_t {
    Null,
    Bool,
    Int64,
    UInt64,
    Float64,
    String,
    Hash,
    ObjectRef
};

struct Value {
    ValueKind kind{ValueKind::Null};
    std::variant<
        std::monostate,
        bool,
        std::int64_t,
        std::uint64_t,
        double,
        std::string,
        Hash256,
        ObjectId>
        data;
};

[[nodiscard]] Value null_value();
[[nodiscard]] Value bool_value(bool value);
[[nodiscard]] Value int64_value(std::int64_t value);
[[nodiscard]] Value uint64_value(std::uint64_t value);
[[nodiscard]] Value float64_value(double value);
[[nodiscard]] Value string_value(std::string value);
[[nodiscard]] Value hash_value(Hash256 value);
[[nodiscard]] Value object_ref_value(ObjectId value);

[[nodiscard]] int compare(const Value& left, const Value& right) noexcept;
[[nodiscard]] bool operator==(const Value& left, const Value& right) noexcept;
[[nodiscard]] bool operator<(const Value& left, const Value& right) noexcept;
[[nodiscard]] std::string to_string(const Value& value);

}  // namespace pv
