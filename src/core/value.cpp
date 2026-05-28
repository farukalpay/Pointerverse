// SPDX-License-Identifier: Apache-2.0
#include "pv/core/value.hpp"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>

namespace pv {
namespace {

int compare_hash(Hash256 left, Hash256 right) noexcept {
    for (std::size_t index = 0; index < left.value.size(); ++index) {
        const auto l = static_cast<unsigned char>(left.value[index]);
        const auto r = static_cast<unsigned char>(right.value[index]);
        if (l < r) {
            return -1;
        }
        if (l > r) {
            return 1;
        }
    }
    return 0;
}

int compare_object(ObjectId left, ObjectId right) noexcept {
    if (left.index < right.index) {
        return -1;
    }
    if (left.index > right.index) {
        return 1;
    }
    if (left.generation < right.generation) {
        return -1;
    }
    if (left.generation > right.generation) {
        return 1;
    }
    return 0;
}

int compare_f64(double left, double right) noexcept {
    const auto l = canonical_f64(left);
    const auto r = canonical_f64(right);
    if (l < r) {
        return -1;
    }
    if (l > r) {
        return 1;
    }
    return 0;
}

}  // namespace

Value null_value() {
    return Value{ValueKind::Null, std::monostate{}};
}

Value bool_value(bool value) {
    return Value{ValueKind::Bool, value};
}

Value int64_value(std::int64_t value) {
    return Value{ValueKind::Int64, value};
}

Value uint64_value(std::uint64_t value) {
    return Value{ValueKind::UInt64, value};
}

Value float64_value(double value) {
    return Value{ValueKind::Float64, value};
}

Value string_value(std::string value) {
    return Value{ValueKind::String, std::move(value)};
}

Value hash_value(Hash256 value) {
    return Value{ValueKind::Hash, value};
}

Value object_ref_value(ObjectId value) {
    return Value{ValueKind::ObjectRef, value};
}

int compare(const Value& left, const Value& right) noexcept {
    if (left.kind != right.kind) {
        return static_cast<std::uint8_t>(left.kind) < static_cast<std::uint8_t>(right.kind) ? -1 : 1;
    }

    switch (left.kind) {
    case ValueKind::Null:
        return 0;
    case ValueKind::Bool: {
        const auto l = std::get<bool>(left.data);
        const auto r = std::get<bool>(right.data);
        return l == r ? 0 : (l ? 1 : -1);
    }
    case ValueKind::Int64: {
        const auto l = std::get<std::int64_t>(left.data);
        const auto r = std::get<std::int64_t>(right.data);
        return l == r ? 0 : (l < r ? -1 : 1);
    }
    case ValueKind::UInt64: {
        const auto l = std::get<std::uint64_t>(left.data);
        const auto r = std::get<std::uint64_t>(right.data);
        return l == r ? 0 : (l < r ? -1 : 1);
    }
    case ValueKind::Float64:
        return compare_f64(std::get<double>(left.data), std::get<double>(right.data));
    case ValueKind::String: {
        const auto& l = std::get<std::string>(left.data);
        const auto& r = std::get<std::string>(right.data);
        return l == r ? 0 : (l < r ? -1 : 1);
    }
    case ValueKind::Hash:
        return compare_hash(std::get<Hash256>(left.data), std::get<Hash256>(right.data));
    case ValueKind::ObjectRef:
        return compare_object(std::get<ObjectId>(left.data), std::get<ObjectId>(right.data));
    }
    return 0;
}

bool operator==(const Value& left, const Value& right) noexcept {
    return compare(left, right) == 0;
}

bool operator<(const Value& left, const Value& right) noexcept {
    return compare(left, right) < 0;
}

std::string to_string(const Value& value) {
    switch (value.kind) {
    case ValueKind::Null:
        return "null";
    case ValueKind::Bool:
        return std::get<bool>(value.data) ? "true" : "false";
    case ValueKind::Int64:
        return std::to_string(std::get<std::int64_t>(value.data));
    case ValueKind::UInt64:
        return std::to_string(std::get<std::uint64_t>(value.data));
    case ValueKind::Float64:
        return fmt::format("{:.17g}", std::get<double>(value.data));
    case ValueKind::String:
        return std::get<std::string>(value.data);
    case ValueKind::Hash:
        return to_hex(std::get<Hash256>(value.data));
    case ValueKind::ObjectRef:
        return to_string(std::get<ObjectId>(value.data));
    }
    return "null";
}

}  // namespace pv
