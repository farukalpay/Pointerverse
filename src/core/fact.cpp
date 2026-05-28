// SPDX-License-Identifier: Apache-2.0
#include "pv/core/fact.hpp"

#include <algorithm>
#include <type_traits>

#include "pv/core/snapshot.hpp"
#include "pv/hash/hasher.hpp"
#include "pv/storage/canonical_codec.hpp"

namespace pv {
namespace {

void encode_object_id_local(CanonicalWriter& writer, ObjectId id) {
    writer.u32(id.index);
    writer.u32(id.generation);
}

void encode_pointer_id_local(CanonicalWriter& writer, PointerId id) {
    writer.u64(id.value);
}

void encode_epoch_local(CanonicalWriter& writer, Epoch epoch) {
    writer.u64(epoch.value);
}

void encode_optional_epoch_local(CanonicalWriter& writer, const std::optional<Epoch>& epoch) {
    writer.u8(epoch.has_value() ? 1 : 0);
    if (epoch.has_value()) {
        encode_epoch_local(writer, *epoch);
    }
}

void encode_value_local(CanonicalWriter& writer, const Value& value) {
    writer.u8(static_cast<std::uint8_t>(value.kind));
    switch (value.kind) {
    case ValueKind::Null:
        return;
    case ValueKind::Bool:
        writer.u8(std::get<bool>(value.data) ? 1 : 0);
        return;
    case ValueKind::Int64:
        writer.i64(std::get<std::int64_t>(value.data));
        return;
    case ValueKind::UInt64:
        writer.u64(std::get<std::uint64_t>(value.data));
        return;
    case ValueKind::Float64:
        writer.f64(std::get<double>(value.data));
        return;
    case ValueKind::String:
        writer.string(std::get<std::string>(value.data));
        return;
    case ValueKind::Hash:
        writer.hash(std::get<Hash256>(value.data));
        return;
    case ValueKind::ObjectRef:
        encode_object_id_local(writer, std::get<ObjectId>(value.data));
        return;
    }
}

void encode_payload(CanonicalWriter& writer, const FactPayload& payload) {
    std::visit(
        [&](const auto& body) {
            using T = std::decay_t<decltype(body)>;
            if constexpr (std::is_same_v<T, ObjectFactPayload>) {
                writer.string("ObjectFactPayload:v1");
                encode_object_id_local(writer, body.object);
                writer.string(body.name);
                writer.u32(body.type.value);
                writer.u8(static_cast<std::uint8_t>(body.existence));
            } else if constexpr (std::is_same_v<T, PointerFactPayload>) {
                writer.string("PointerFactPayload:v1");
                encode_pointer_id_local(writer, body.pointer);
                encode_object_id_local(writer, body.from);
                encode_object_id_local(writer, body.to);
                writer.u32(body.relation.id);
                writer.u8(static_cast<std::uint8_t>(body.causal_role));
                writer.f64(body.weight.value);
                encode_epoch_local(writer, body.born_at);
                encode_optional_epoch_local(writer, body.expires_at);
                writer.string(body.law_domain);
            } else if constexpr (std::is_same_v<T, AttributeFactPayload>) {
                writer.string("AttributeFactPayload:v1");
                if (const auto* subject = std::get_if<ObjectAttributeSubject>(&body.subject)) {
                    writer.u8(0);
                    encode_object_id_local(writer, subject->object);
                } else {
                    writer.u8(1);
                    encode_pointer_id_local(writer, std::get<PointerAttributeSubject>(body.subject).pointer);
                }
                writer.string(body.key);
                encode_value_local(writer, body.value);
            } else if constexpr (std::is_same_v<T, EvidenceFactPayload>) {
                writer.string("EvidenceFactPayload:v1");
                writer.hash(body.evidence);
                writer.string(body.label);
            } else if constexpr (std::is_same_v<T, LawFactPayload>) {
                writer.string("LawFactPayload:v1");
                writer.string(body.law);
                writer.u8(body.passed ? 1 : 0);
                writer.hash(body.input_hash);
                writer.hash(body.output_hash);
            }
        },
        payload);
}

Hash256 payload_hash(const FactPayload& payload) {
    CanonicalWriter writer;
    encode_payload(writer, payload);
    return sha256(writer.bytes());
}

std::vector<Attribute> sorted_attributes(std::vector<Attribute> attributes) {
    sort_attributes(attributes);
    return attributes;
}

}  // namespace

bool operator<(const FactId& left, const FactId& right) noexcept {
    for (std::size_t index = 0; index < left.value.value.size(); ++index) {
        const auto l = static_cast<unsigned char>(left.value.value[index]);
        const auto r = static_cast<unsigned char>(right.value.value[index]);
        if (l < r) {
            return true;
        }
        if (l > r) {
            return false;
        }
    }
    return false;
}

bool operator<(const Fact& left, const Fact& right) noexcept {
    return left.id < right.id;
}

Fact make_fact(FactKind kind, Epoch born_at, std::optional<Epoch> expired_at, FactPayload payload) {
    const auto hashed_payload = payload_hash(payload);
    CanonicalWriter writer;
    writer.string("FactId:v1");
    writer.u8(static_cast<std::uint8_t>(kind));
    encode_epoch_local(writer, born_at);
    encode_optional_epoch_local(writer, expired_at);
    writer.hash(hashed_payload);
    return Fact{FactId{sha256(writer.bytes())}, kind, born_at, expired_at, hashed_payload, std::move(payload)};
}

std::vector<Fact> derive_facts(const WorldSnapshot& snapshot) {
    std::vector<Fact> facts;

    auto objects = snapshot.objects;
    std::ranges::sort(objects, [](const ObjectSnapshot& left, const ObjectSnapshot& right) {
        if (left.id.index != right.id.index) {
            return left.id.index < right.id.index;
        }
        return left.id.generation < right.id.generation;
    });
    for (const auto& object : objects) {
        facts.push_back(make_fact(
            FactKind::ObjectFact,
            Epoch{0},
            object.existence == ExistenceState::Tombstoned ? std::optional<Epoch>{snapshot.epoch} : std::nullopt,
            ObjectFactPayload{object.id, object.name, object.type, object.existence}));
        for (const auto& attribute : sorted_attributes(object.attributes)) {
            facts.push_back(make_fact(
                FactKind::AttributeFact,
                Epoch{0},
                std::nullopt,
                AttributeFactPayload{AttributeSubject{ObjectAttributeSubject{object.id}}, attribute.key, attribute.value}));
        }
    }

    auto pointers = snapshot.pointers;
    std::ranges::sort(pointers, [](const PointerSnapshot& left, const PointerSnapshot& right) {
        return left.id.value < right.id.value;
    });
    for (const auto& pointer : pointers) {
        facts.push_back(make_fact(
            FactKind::PointerFact,
            pointer.born_at,
            pointer.expires_at,
            PointerFactPayload{
                pointer.id,
                pointer.from,
                pointer.to,
                pointer.relation,
                pointer.causal_role,
                pointer.weight,
                pointer.born_at,
                pointer.expires_at,
                pointer.law_domain}));
        for (const auto& attribute : sorted_attributes(pointer.attributes)) {
            facts.push_back(make_fact(
                FactKind::AttributeFact,
                pointer.born_at,
                pointer.expires_at,
                AttributeFactPayload{AttributeSubject{PointerAttributeSubject{pointer.id}}, attribute.key, attribute.value}));
        }
    }

    std::ranges::sort(facts, [](const Fact& left, const Fact& right) {
        return left < right;
    });
    return facts;
}

std::string to_string(FactKind kind) {
    switch (kind) {
    case FactKind::ObjectFact:
        return "ObjectFact";
    case FactKind::PointerFact:
        return "PointerFact";
    case FactKind::AttributeFact:
        return "AttributeFact";
    case FactKind::EvidenceFact:
        return "EvidenceFact";
    case FactKind::LawFact:
        return "LawFact";
    }
    return "ObjectFact";
}

std::string to_string(FactId id) {
    return to_hex(id.value);
}

}  // namespace pv
