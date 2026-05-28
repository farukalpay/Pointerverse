// SPDX-License-Identifier: Apache-2.0
#include "pv/hash/canonical.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <variant>

#include "pv/core/delta.hpp"
#include "pv/core/snapshot.hpp"
#include "pv/hash/hasher.hpp"
#include "pv/law/law.hpp"
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

void write_object_id(CanonicalHasher& hasher, ObjectId id) {
    hasher.write_u32(id.index);
    hasher.write_u32(id.generation);
}

void write_pointer_id(CanonicalHasher& hasher, PointerId id) {
    hasher.write_u64(id.value);
}

void write_epoch(CanonicalHasher& hasher, Epoch epoch) {
    hasher.write_u64(epoch.value);
}

void write_world_id(CanonicalHasher& hasher, WorldId id) {
    hasher.write_u64(id.value);
}

void write_type_id(CanonicalHasher& hasher, TypeId id) {
    hasher.write_u32(id.value);
}

void write_relation_type(CanonicalHasher& hasher, RelationType relation) {
    hasher.write_u32(relation.id);
}

void write_existence(CanonicalHasher& hasher, ExistenceState existence) {
    hasher.write_u8(static_cast<std::uint8_t>(existence));
}

void write_causal_role(CanonicalHasher& hasher, CausalRole role) {
    hasher.write_u8(static_cast<std::uint8_t>(role));
}

void write_optional_epoch(CanonicalHasher& hasher, const std::optional<Epoch>& epoch) {
    hasher.write_u8(epoch.has_value() ? 1 : 0);
    if (epoch.has_value()) {
        write_epoch(hasher, *epoch);
    }
}

void write_optional_type(CanonicalHasher& hasher, const std::optional<TypeId>& type) {
    hasher.write_u8(type.has_value() ? 1 : 0);
    if (type.has_value()) {
        write_type_id(hasher, *type);
    }
}

void write_optional_existence(CanonicalHasher& hasher, const std::optional<ExistenceState>& existence) {
    hasher.write_u8(existence.has_value() ? 1 : 0);
    if (existence.has_value()) {
        write_existence(hasher, *existence);
    }
}

void write_ref(CanonicalHasher& hasher, const ObjectRef& ref) {
    if (const auto* id = std::get_if<ObjectId>(&ref)) {
        hasher.write_u8(0);
        write_object_id(hasher, *id);
        return;
    }
    hasher.write_u8(1);
    hasher.write_u32(std::get<TempObjectId>(ref).value);
}

void write_trace_event(CanonicalHasher& hasher, const TraceEvent& event) {
    hasher.write_string("TraceEvent:v1");
    write_epoch(hasher, event.epoch);
    hasher.write_string(event.event);
    hasher.write_u64(event.fields.size());
    for (const auto& [key, value] : event.fields) {
        hasher.write_string(key);
        hasher.write_string(value);
    }
    hasher.write_u64(event.measurements.size());
    for (const auto& [key, value] : event.measurements) {
        hasher.write_string(key);
        hasher.write_f64(value);
    }
}

void write_law_status(CanonicalHasher& hasher, const LawStatus& status) {
    hasher.write_string(status.law);
    hasher.write_u8(status.passed ? 1 : 0);
    hasher.write_u8(static_cast<std::uint8_t>(status.severity));
    hasher.write_f64(status.magnitude);
    hasher.write_string(status.explanation);
}

void write_law_violation(CanonicalHasher& hasher, const LawViolation& violation) {
    hasher.write_string(violation.law);
    hasher.write_u8(static_cast<std::uint8_t>(violation.severity));
    hasher.write_f64(violation.magnitude);
    hasher.write_string(violation.explanation);
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
    CanonicalHasher hasher;
    hasher.write_string("WorldSnapshot:v1");
    write_world_id(hasher, snapshot.world);
    hasher.write_string(snapshot.world_name);
    write_epoch(hasher, snapshot.epoch);

    std::set<std::uint32_t> used_type_ids;
    for (const auto& object : snapshot.objects) {
        used_type_ids.insert(object.type.value);
    }
    hasher.write_u64(used_type_ids.size());
    for (const auto id : used_type_ids) {
        hasher.write_u32(id);
        if (const auto iter = snapshot.type_names.find(id); iter != snapshot.type_names.end()) {
            hasher.write_string(iter->second);
        } else {
            hasher.write_string({});
        }
    }

    std::set<std::uint32_t> used_relation_ids;
    for (const auto& pointer : snapshot.pointers) {
        used_relation_ids.insert(pointer.relation.id);
    }
    hasher.write_u64(used_relation_ids.size());
    for (const auto id : used_relation_ids) {
        hasher.write_u32(id);
        if (const auto iter = snapshot.relation_names.find(id); iter != snapshot.relation_names.end()) {
            hasher.write_string(iter->second);
        } else {
            hasher.write_string({});
        }
    }

    auto objects = snapshot.objects;
    std::ranges::sort(objects, [](const ObjectSnapshot& left, const ObjectSnapshot& right) {
        if (left.id.index != right.id.index) {
            return left.id.index < right.id.index;
        }
        return left.id.generation < right.id.generation;
    });
    hasher.write_u64(objects.size());
    for (const auto& object : objects) {
        write_object_id(hasher, object.id);
        hasher.write_string(object.name);
        write_type_id(hasher, object.type);
        write_existence(hasher, object.existence);
        hasher.write_u64(object.incoming_count);
        hasher.write_u64(object.outgoing_count);
    }

    auto pointers = snapshot.pointers;
    std::ranges::sort(pointers, [](const PointerSnapshot& left, const PointerSnapshot& right) {
        return left.id.value < right.id.value;
    });
    hasher.write_u64(pointers.size());
    for (const auto& pointer : pointers) {
        write_pointer_id(hasher, pointer.id);
        write_object_id(hasher, pointer.from);
        write_object_id(hasher, pointer.to);
        write_relation_type(hasher, pointer.relation);
        write_causal_role(hasher, pointer.causal_role);
        hasher.write_f64(pointer.weight.value);
        write_epoch(hasher, pointer.born_at);
        write_optional_epoch(hasher, pointer.expires_at);
        hasher.write_string(pointer.law_domain);
    }

    return hasher.finish();
}

Hash256 canonical_hash(const Delta& delta) {
    CanonicalHasher hasher;
    hasher.write_string("Delta:v1");

    hasher.write_u64(delta.creates.size());
    for (const auto& create : delta.creates) {
        hasher.write_u32(create.temp_id.value);
        hasher.write_string(create.name);
        write_type_id(hasher, create.type);
        write_existence(hasher, create.existence);
    }

    hasher.write_u64(delta.updates.size());
    for (const auto& update : delta.updates) {
        write_ref(hasher, update.object);
        write_optional_type(hasher, update.type);
        write_optional_existence(hasher, update.existence);
    }

    hasher.write_u64(delta.links.size());
    for (const auto& link : delta.links) {
        write_ref(hasher, link.from);
        write_ref(hasher, link.to);
        write_relation_type(hasher, link.relation);
        write_causal_role(hasher, link.causal_role);
        hasher.write_f64(link.weight.value);
        hasher.write_string(link.law_domain);
    }

    hasher.write_u64(delta.unlinks.size());
    for (const auto& unlink : delta.unlinks) {
        write_pointer_id(hasher, unlink.id);
    }

    hasher.write_u64(delta.events.size());
    for (const auto& event : delta.events) {
        write_trace_event(hasher, event);
    }

    return hasher.finish();
}

Hash256 canonical_hash(const TraceEvent& event) {
    CanonicalHasher hasher;
    write_trace_event(hasher, event);
    return hasher.finish();
}

Hash256 canonical_hash(const std::vector<TraceEvent>& events) {
    CanonicalHasher hasher;
    hasher.write_string("TraceEvents:v1");
    hasher.write_u64(events.size());
    for (const auto& event : events) {
        write_trace_event(hasher, event);
    }
    return hasher.finish();
}

Hash256 canonical_hash(const std::vector<LawStatus>& statuses) {
    CanonicalHasher hasher;
    hasher.write_string("LawStatuses:v1");
    hasher.write_u64(statuses.size());
    for (const auto& status : statuses) {
        write_law_status(hasher, status);
    }
    return hasher.finish();
}

Hash256 canonical_hash(const std::vector<LawViolation>& violations) {
    CanonicalHasher hasher;
    hasher.write_string("LawViolations:v1");
    hasher.write_u64(violations.size());
    for (const auto& violation : violations) {
        write_law_violation(hasher, violation);
    }
    return hasher.finish();
}

Hash256 canonical_hash_morphism_path(const std::vector<std::string>& path) {
    CanonicalHasher hasher;
    hasher.write_string("MorphismPath:v1");
    hasher.write_u64(path.size());
    for (const auto& item : path) {
        hasher.write_string(item);
    }
    return hasher.finish();
}

}  // namespace pv
