// SPDX-License-Identifier: Apache-2.0
#include "pv/storage/canonical_codec.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <variant>

namespace pv {
namespace {

void write_big_endian(std::vector<std::byte>& bytes, std::uint64_t value, std::size_t width) {
    for (std::size_t shift = width; shift > 0; --shift) {
        bytes.push_back(static_cast<std::byte>((value >> ((shift - 1U) * 8U)) & 0xffU));
    }
}

std::uint64_t read_big_endian(std::span<const std::byte> bytes) {
    std::uint64_t out = 0;
    for (const auto byte : bytes) {
        out = (out << 8U) | static_cast<unsigned char>(byte);
    }
    return out;
}

void encode_object_id(CanonicalWriter& writer, ObjectId id) {
    writer.u32(id.index);
    writer.u32(id.generation);
}

ObjectId decode_object_id(CanonicalReader& reader) {
    return ObjectId{reader.u32(), reader.u32()};
}

void encode_pointer_id(CanonicalWriter& writer, PointerId id) {
    writer.u64(id.value);
}

PointerId decode_pointer_id(CanonicalReader& reader) {
    return PointerId{reader.u64()};
}

void encode_epoch(CanonicalWriter& writer, Epoch epoch) {
    writer.u64(epoch.value);
}

Epoch decode_epoch(CanonicalReader& reader) {
    return Epoch{reader.u64()};
}

void encode_world_id(CanonicalWriter& writer, WorldId id) {
    writer.u64(id.value);
}

WorldId decode_world_id(CanonicalReader& reader) {
    return WorldId{reader.u64()};
}

void encode_branch_id(CanonicalWriter& writer, BranchId id) {
    writer.u64(id.value);
}

BranchId decode_branch_id(CanonicalReader& reader) {
    return BranchId{reader.u64()};
}

void encode_transaction_id(CanonicalWriter& writer, TransactionId id) {
    writer.u64(id.value);
}

TransactionId decode_transaction_id(CanonicalReader& reader) {
    return TransactionId{reader.u64()};
}

void encode_type_id(CanonicalWriter& writer, TypeId id) {
    writer.u32(id.value);
}

TypeId decode_type_id(CanonicalReader& reader) {
    return TypeId{reader.u32()};
}

void encode_relation_type(CanonicalWriter& writer, RelationType relation) {
    writer.u32(relation.id);
}

RelationType decode_relation_type(CanonicalReader& reader) {
    return RelationType{reader.u32()};
}

void encode_existence(CanonicalWriter& writer, ExistenceState existence) {
    writer.u8(static_cast<std::uint8_t>(existence));
}

ExistenceState decode_existence(CanonicalReader& reader) {
    return static_cast<ExistenceState>(reader.u8());
}

void encode_causal_role(CanonicalWriter& writer, CausalRole role) {
    writer.u8(static_cast<std::uint8_t>(role));
}

CausalRole decode_causal_role(CanonicalReader& reader) {
    return static_cast<CausalRole>(reader.u8());
}

void encode_severity(CanonicalWriter& writer, Severity severity) {
    writer.u8(static_cast<std::uint8_t>(severity));
}

Severity decode_severity(CanonicalReader& reader) {
    return static_cast<Severity>(reader.u8());
}

void encode_optional_epoch(CanonicalWriter& writer, const std::optional<Epoch>& epoch) {
    writer.u8(epoch.has_value() ? 1 : 0);
    if (epoch.has_value()) {
        encode_epoch(writer, *epoch);
    }
}

std::optional<Epoch> decode_optional_epoch(CanonicalReader& reader) {
    if (reader.u8() == 0) {
        return std::nullopt;
    }
    return decode_epoch(reader);
}

std::optional<TypeId> decode_optional_type(CanonicalReader& reader) {
    if (reader.u8() == 0) {
        return std::nullopt;
    }
    return decode_type_id(reader);
}

std::optional<ExistenceState> decode_optional_existence(CanonicalReader& reader) {
    if (reader.u8() == 0) {
        return std::nullopt;
    }
    return decode_existence(reader);
}

void encode_ref(CanonicalWriter& writer, const ObjectRef& ref) {
    if (const auto* id = std::get_if<ObjectId>(&ref)) {
        writer.u8(0);
        encode_object_id(writer, *id);
        return;
    }
    writer.u8(1);
    writer.u32(std::get<TempObjectId>(ref).value);
}

ObjectRef decode_ref(CanonicalReader& reader) {
    const auto tag = reader.u8();
    if (tag == 0) {
        return ObjectRef{decode_object_id(reader)};
    }
    if (tag == 1) {
        return ObjectRef{TempObjectId{reader.u32()}};
    }
    throw std::runtime_error("invalid object reference tag in canonical stream");
}

void encode_commit_id(CanonicalWriter& writer, const CommitId& id) {
    writer.hash(id.value);
}

CommitId decode_commit_id(CanonicalReader& reader) {
    return CommitId{reader.hash()};
}

void encode_optional_commit_id(CanonicalWriter& writer, const std::optional<CommitId>& id) {
    writer.u8(id.has_value() ? 1 : 0);
    if (id.has_value()) {
        encode_commit_id(writer, *id);
    }
}

std::optional<CommitId> decode_optional_commit_id(CanonicalReader& reader) {
    if (reader.u8() == 0) {
        return std::nullopt;
    }
    return decode_commit_id(reader);
}

std::uint64_t checked_count(std::uint64_t value) {
    if (value > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error("canonical sequence length exceeds platform size");
    }
    return value;
}

}  // namespace

void CanonicalWriter::u8(std::uint8_t value) {
    bytes_.push_back(static_cast<std::byte>(value));
}

void CanonicalWriter::u32(std::uint32_t value) {
    write_big_endian(bytes_, value, 4);
}

void CanonicalWriter::u64(std::uint64_t value) {
    write_big_endian(bytes_, value, 8);
}

void CanonicalWriter::i64(std::int64_t value) {
    u64(static_cast<std::uint64_t>(value));
}

void CanonicalWriter::f64(double value) {
    u64(canonical_f64(value));
}

void CanonicalWriter::string(std::string_view value) {
    u64(value.size());
    for (const auto ch : value) {
        bytes_.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }
}

void CanonicalWriter::hash(Hash256 value) {
    bytes(value.value);
}

void CanonicalWriter::bytes(std::span<const std::byte> value) {
    bytes_.insert(bytes_.end(), value.begin(), value.end());
}

void CanonicalWriter::sized_bytes(std::span<const std::byte> value) {
    u64(value.size());
    bytes(value);
}

std::span<const std::byte> CanonicalWriter::bytes() const noexcept {
    return bytes_;
}

std::vector<std::byte> CanonicalWriter::take_bytes() {
    return std::move(bytes_);
}

CanonicalReader::CanonicalReader(std::span<const std::byte> bytes) : bytes_(bytes) {}

std::span<const std::byte> CanonicalReader::read(std::size_t count) {
    if (count > bytes_.size() - offset_) {
        throw std::runtime_error("canonical stream ended unexpectedly");
    }
    const auto out = bytes_.subspan(offset_, count);
    offset_ += count;
    return out;
}

std::uint8_t CanonicalReader::u8() {
    return static_cast<std::uint8_t>(read(1).front());
}

std::uint32_t CanonicalReader::u32() {
    return static_cast<std::uint32_t>(read_big_endian(read(4)));
}

std::uint64_t CanonicalReader::u64() {
    return read_big_endian(read(8));
}

std::int64_t CanonicalReader::i64() {
    return static_cast<std::int64_t>(u64());
}

double CanonicalReader::f64() {
    const auto bits = u64();
    double out = 0.0;
    std::memcpy(&out, &bits, sizeof(out));
    return out;
}

std::string CanonicalReader::string() {
    const auto size = static_cast<std::size_t>(checked_count(u64()));
    const auto data = read(size);
    std::string out;
    out.reserve(size);
    for (const auto byte : data) {
        out.push_back(static_cast<char>(static_cast<unsigned char>(byte)));
    }
    return out;
}

Hash256 CanonicalReader::hash() {
    Hash256 out;
    const auto data = read(out.value.size());
    std::copy(data.begin(), data.end(), out.value.begin());
    return out;
}

std::vector<std::byte> CanonicalReader::sized_bytes() {
    const auto size = static_cast<std::size_t>(checked_count(u64()));
    const auto data = read(size);
    return {data.begin(), data.end()};
}

void CanonicalReader::expect_end() const {
    if (!eof()) {
        throw std::runtime_error("canonical stream has trailing bytes");
    }
}

void CanonicalReader::expect_tag(std::string_view tag) {
    if (string() != tag) {
        throw std::runtime_error("canonical stream has unexpected type tag");
    }
}

bool CanonicalReader::eof() const noexcept {
    return offset_ == bytes_.size();
}

void encode(CanonicalWriter& writer, const TraceEvent& event) {
    writer.string("TraceEvent:v1");
    encode_epoch(writer, event.epoch);
    writer.string(event.event);
    writer.u64(event.fields.size());
    for (const auto& [key, value] : event.fields) {
        writer.string(key);
        writer.string(value);
    }
    writer.u64(event.measurements.size());
    for (const auto& [key, value] : event.measurements) {
        writer.string(key);
        writer.f64(value);
    }
}

TraceEvent decode_trace_event(CanonicalReader& reader) {
    reader.expect_tag("TraceEvent:v1");
    TraceEvent event;
    event.epoch = decode_epoch(reader);
    event.event = reader.string();
    const auto field_count = checked_count(reader.u64());
    for (std::uint64_t index = 0; index < field_count; ++index) {
        event.fields.emplace(reader.string(), reader.string());
    }
    const auto measurement_count = checked_count(reader.u64());
    for (std::uint64_t index = 0; index < measurement_count; ++index) {
        event.measurements.emplace(reader.string(), reader.f64());
    }
    return event;
}

void encode(CanonicalWriter& writer, const LawStatus& status) {
    writer.string(status.law);
    writer.u8(status.passed ? 1 : 0);
    encode_severity(writer, status.severity);
    writer.f64(status.magnitude);
    writer.string(status.explanation);
}

LawStatus decode_law_status(CanonicalReader& reader) {
    LawStatus status;
    status.law = reader.string();
    status.passed = reader.u8() != 0;
    status.severity = decode_severity(reader);
    status.magnitude = reader.f64();
    status.explanation = reader.string();
    return status;
}

void encode(CanonicalWriter& writer, const LawViolation& violation) {
    writer.string(violation.law);
    encode_severity(writer, violation.severity);
    writer.f64(violation.magnitude);
    writer.string(violation.explanation);
    writer.u64(violation.evidence.size());
    for (const auto& fact : violation.evidence) {
        writer.hash(fact.value);
    }
    writer.u64(violation.objects.size());
    for (const auto& object : violation.objects) {
        encode_object_id(writer, object);
    }
    writer.u64(violation.pointers.size());
    for (const auto& pointer : violation.pointers) {
        encode_pointer_id(writer, pointer);
    }
}

LawViolation decode_law_violation(CanonicalReader& reader) {
    LawViolation violation;
    violation.law = reader.string();
    violation.severity = decode_severity(reader);
    violation.magnitude = reader.f64();
    violation.explanation = reader.string();
    const auto evidence_count = checked_count(reader.u64());
    violation.evidence.reserve(static_cast<std::size_t>(evidence_count));
    for (std::uint64_t index = 0; index < evidence_count; ++index) {
        violation.evidence.push_back(FactId{reader.hash()});
    }
    const auto object_count = checked_count(reader.u64());
    violation.objects.reserve(static_cast<std::size_t>(object_count));
    for (std::uint64_t index = 0; index < object_count; ++index) {
        violation.objects.push_back(decode_object_id(reader));
    }
    const auto pointer_count = checked_count(reader.u64());
    violation.pointers.reserve(static_cast<std::size_t>(pointer_count));
    for (std::uint64_t index = 0; index < pointer_count; ++index) {
        violation.pointers.push_back(decode_pointer_id(reader));
    }
    return violation;
}

void encode(CanonicalWriter& writer, const Value& value) {
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
        encode_object_id(writer, std::get<ObjectId>(value.data));
        return;
    }
}

Value decode_value(CanonicalReader& reader) {
    const auto kind = static_cast<ValueKind>(reader.u8());
    switch (kind) {
    case ValueKind::Null:
        return null_value();
    case ValueKind::Bool:
        return bool_value(reader.u8() != 0);
    case ValueKind::Int64:
        return int64_value(reader.i64());
    case ValueKind::UInt64:
        return uint64_value(reader.u64());
    case ValueKind::Float64:
        return float64_value(reader.f64());
    case ValueKind::String:
        return string_value(reader.string());
    case ValueKind::Hash:
        return hash_value(reader.hash());
    case ValueKind::ObjectRef:
        return object_ref_value(decode_object_id(reader));
    }
    throw std::runtime_error("invalid value kind in canonical stream");
}

void encode(CanonicalWriter& writer, const Attribute& attribute) {
    writer.string(attribute.key);
    encode(writer, attribute.value);
}

Attribute decode_attribute(CanonicalReader& reader) {
    return Attribute{reader.string(), decode_value(reader)};
}

void encode_fact_payload(CanonicalWriter& writer, const FactPayload& payload) {
    std::visit(
        [&](const auto& body) {
            using T = std::decay_t<decltype(body)>;
            if constexpr (std::is_same_v<T, ObjectFactPayload>) {
                writer.u8(0);
                encode_object_id(writer, body.object);
                writer.string(body.name);
                encode_type_id(writer, body.type);
                encode_existence(writer, body.existence);
            } else if constexpr (std::is_same_v<T, PointerFactPayload>) {
                writer.u8(1);
                encode_pointer_id(writer, body.pointer);
                encode_object_id(writer, body.from);
                encode_object_id(writer, body.to);
                encode_relation_type(writer, body.relation);
                encode_causal_role(writer, body.causal_role);
                writer.f64(body.weight.value);
                encode_epoch(writer, body.born_at);
                encode_optional_epoch(writer, body.expires_at);
                writer.string(body.law_domain);
            } else if constexpr (std::is_same_v<T, AttributeFactPayload>) {
                writer.u8(2);
                if (const auto* object = std::get_if<ObjectAttributeSubject>(&body.subject)) {
                    writer.u8(0);
                    encode_object_id(writer, object->object);
                } else {
                    writer.u8(1);
                    encode_pointer_id(writer, std::get<PointerAttributeSubject>(body.subject).pointer);
                }
                writer.string(body.key);
                encode(writer, body.value);
            } else if constexpr (std::is_same_v<T, EvidenceFactPayload>) {
                writer.u8(3);
                writer.hash(body.evidence);
                writer.string(body.label);
            } else if constexpr (std::is_same_v<T, LawFactPayload>) {
                writer.u8(4);
                writer.string(body.law);
                writer.u8(body.passed ? 1 : 0);
                writer.hash(body.input_hash);
                writer.hash(body.output_hash);
            }
        },
        payload);
}

FactPayload decode_fact_payload(CanonicalReader& reader) {
    const auto tag = reader.u8();
    if (tag == 0) {
        return ObjectFactPayload{
            decode_object_id(reader),
            reader.string(),
            decode_type_id(reader),
            decode_existence(reader)
        };
    }
    if (tag == 1) {
        return PointerFactPayload{
            decode_pointer_id(reader),
            decode_object_id(reader),
            decode_object_id(reader),
            decode_relation_type(reader),
            decode_causal_role(reader),
            Weight{reader.f64()},
            decode_epoch(reader),
            decode_optional_epoch(reader),
            reader.string()
        };
    }
    if (tag == 2) {
        AttributeSubject subject;
        const auto subject_tag = reader.u8();
        if (subject_tag == 0) {
            subject = ObjectAttributeSubject{decode_object_id(reader)};
        } else if (subject_tag == 1) {
            subject = PointerAttributeSubject{decode_pointer_id(reader)};
        } else {
            throw std::runtime_error("invalid attribute fact subject tag");
        }
        return AttributeFactPayload{std::move(subject), reader.string(), decode_value(reader)};
    }
    if (tag == 3) {
        return EvidenceFactPayload{reader.hash(), reader.string()};
    }
    if (tag == 4) {
        return LawFactPayload{reader.string(), reader.u8() != 0, reader.hash(), reader.hash()};
    }
    throw std::runtime_error("invalid fact payload tag");
}

void encode(CanonicalWriter& writer, const Fact& fact) {
    writer.hash(fact.id.value);
    writer.u8(static_cast<std::uint8_t>(fact.kind));
    encode_epoch(writer, fact.born_at);
    encode_optional_epoch(writer, fact.expired_at);
    writer.hash(fact.payload_hash);
    encode_fact_payload(writer, fact.payload);
}

Fact decode_fact(CanonicalReader& reader) {
    Fact fact;
    fact.id = FactId{reader.hash()};
    fact.kind = static_cast<FactKind>(reader.u8());
    fact.born_at = decode_epoch(reader);
    fact.expired_at = decode_optional_epoch(reader);
    fact.payload_hash = reader.hash();
    fact.payload = decode_fact_payload(reader);
    return fact;
}

void encode(CanonicalWriter& writer, const WorldSnapshot& snapshot) {
    writer.string("WorldSnapshot:v2");
    encode_world_id(writer, snapshot.world);
    writer.string(snapshot.world_name);
    encode_epoch(writer, snapshot.epoch);

    std::set<std::uint32_t> used_type_ids;
    for (const auto& object : snapshot.objects) {
        used_type_ids.insert(object.type.value);
    }
    writer.u64(used_type_ids.size());
    for (const auto id : used_type_ids) {
        writer.u32(id);
        if (const auto iter = snapshot.type_names.find(id); iter != snapshot.type_names.end()) {
            writer.string(iter->second);
        } else {
            writer.string({});
        }
    }

    std::set<std::uint32_t> used_relation_ids;
    for (const auto& pointer : snapshot.pointers) {
        used_relation_ids.insert(pointer.relation.id);
    }
    writer.u64(used_relation_ids.size());
    for (const auto id : used_relation_ids) {
        writer.u32(id);
        if (const auto iter = snapshot.relation_names.find(id); iter != snapshot.relation_names.end()) {
            writer.string(iter->second);
        } else {
            writer.string({});
        }
    }

    auto objects = snapshot.objects;
    std::ranges::sort(objects, [](const ObjectSnapshot& left, const ObjectSnapshot& right) {
        if (left.id.index != right.id.index) {
            return left.id.index < right.id.index;
        }
        return left.id.generation < right.id.generation;
    });
    writer.u64(objects.size());
    for (const auto& object : objects) {
        encode_object_id(writer, object.id);
        writer.string(object.name);
        encode_type_id(writer, object.type);
        encode_existence(writer, object.existence);
        auto attributes = object.attributes;
        sort_attributes(attributes);
        writer.u64(attributes.size());
        for (const auto& attribute : attributes) {
            encode(writer, attribute);
        }
        writer.u64(object.incoming_count);
        writer.u64(object.outgoing_count);
    }

    auto pointers = snapshot.pointers;
    std::ranges::sort(pointers, [](const PointerSnapshot& left, const PointerSnapshot& right) {
        return left.id.value < right.id.value;
    });
    writer.u64(pointers.size());
    for (const auto& pointer : pointers) {
        encode_pointer_id(writer, pointer.id);
        encode_object_id(writer, pointer.from);
        encode_object_id(writer, pointer.to);
        encode_relation_type(writer, pointer.relation);
        encode_causal_role(writer, pointer.causal_role);
        writer.f64(pointer.weight.value);
        encode_epoch(writer, pointer.born_at);
        encode_optional_epoch(writer, pointer.expires_at);
        writer.string(pointer.law_domain);
        auto attributes = pointer.attributes;
        sort_attributes(attributes);
        writer.u64(attributes.size());
        for (const auto& attribute : attributes) {
            encode(writer, attribute);
        }
    }

    auto facts = snapshot.facts.empty() ? derive_facts(snapshot) : snapshot.facts;
    std::ranges::sort(facts, [](const Fact& left, const Fact& right) {
        return left < right;
    });
    writer.u64(facts.size());
    for (const auto& fact : facts) {
        encode(writer, fact);
    }
}

WorldSnapshot decode_world_snapshot(CanonicalReader& reader) {
    const auto tag = reader.string();
    if (tag != "WorldSnapshot:v1" && tag != "WorldSnapshot:v2") {
        throw std::runtime_error("canonical stream has unexpected type tag");
    }
    WorldSnapshot snapshot;
    snapshot.world = decode_world_id(reader);
    snapshot.world_name = reader.string();
    snapshot.epoch = decode_epoch(reader);

    const auto type_count = checked_count(reader.u64());
    for (std::uint64_t index = 0; index < type_count; ++index) {
        snapshot.type_names.emplace(reader.u32(), reader.string());
    }

    const auto relation_count = checked_count(reader.u64());
    for (std::uint64_t index = 0; index < relation_count; ++index) {
        snapshot.relation_names.emplace(reader.u32(), reader.string());
    }

    const auto object_count = checked_count(reader.u64());
    snapshot.objects.reserve(static_cast<std::size_t>(object_count));
    for (std::uint64_t index = 0; index < object_count; ++index) {
        ObjectSnapshot object;
        object.id = decode_object_id(reader);
        object.name = reader.string();
        object.type = decode_type_id(reader);
        object.existence = decode_existence(reader);
        if (tag == "WorldSnapshot:v2") {
            const auto attribute_count = checked_count(reader.u64());
            object.attributes.reserve(static_cast<std::size_t>(attribute_count));
            for (std::uint64_t attribute_index = 0; attribute_index < attribute_count; ++attribute_index) {
                object.attributes.push_back(decode_attribute(reader));
            }
        }
        object.incoming_count = static_cast<std::size_t>(checked_count(reader.u64()));
        object.outgoing_count = static_cast<std::size_t>(checked_count(reader.u64()));
        snapshot.objects.push_back(std::move(object));
    }

    const auto pointer_count = checked_count(reader.u64());
    snapshot.pointers.reserve(static_cast<std::size_t>(pointer_count));
    for (std::uint64_t index = 0; index < pointer_count; ++index) {
        PointerSnapshot pointer;
        pointer.id = decode_pointer_id(reader);
        pointer.from = decode_object_id(reader);
        pointer.to = decode_object_id(reader);
        pointer.relation = decode_relation_type(reader);
        pointer.causal_role = decode_causal_role(reader);
        pointer.weight = Weight{reader.f64()};
        pointer.born_at = decode_epoch(reader);
        pointer.expires_at = decode_optional_epoch(reader);
        pointer.law_domain = reader.string();
        if (tag == "WorldSnapshot:v2") {
            const auto attribute_count = checked_count(reader.u64());
            pointer.attributes.reserve(static_cast<std::size_t>(attribute_count));
            for (std::uint64_t attribute_index = 0; attribute_index < attribute_count; ++attribute_index) {
                pointer.attributes.push_back(decode_attribute(reader));
            }
        }
        snapshot.pointers.push_back(std::move(pointer));
    }
    if (tag == "WorldSnapshot:v2") {
        const auto fact_count = checked_count(reader.u64());
        snapshot.facts.reserve(static_cast<std::size_t>(fact_count));
        for (std::uint64_t index = 0; index < fact_count; ++index) {
            snapshot.facts.push_back(decode_fact(reader));
        }
    }
    return snapshot;
}

void encode(CanonicalWriter& writer, const Delta& delta) {
    writer.string("Delta:v2");
    writer.u64(delta.ops.size());
    for (const auto& op : delta.ops) {
        writer.u64(op.id.value);
        writer.u8(static_cast<std::uint8_t>(op.kind));
        switch (op.kind) {
        case OperationKind::CreateObject: {
            const auto& body = std::get<CreateObjectOp>(op.body);
            writer.u32(body.temp_id.value);
            writer.string(body.name);
            encode_type_id(writer, body.type);
            encode_existence(writer, body.existence);
            auto attributes = body.attributes;
            sort_attributes(attributes);
            writer.u64(attributes.size());
            for (const auto& attribute : attributes) {
                encode(writer, attribute);
            }
            break;
        }
        case OperationKind::SetObjectType: {
            const auto& body = std::get<SetObjectTypeOp>(op.body);
            encode_ref(writer, body.object);
            encode_type_id(writer, body.type);
            break;
        }
        case OperationKind::SetObjectExistence: {
            const auto& body = std::get<SetObjectExistenceOp>(op.body);
            encode_ref(writer, body.object);
            encode_existence(writer, body.existence);
            break;
        }
        case OperationKind::SetObjectAttribute: {
            const auto& body = std::get<SetObjectAttributeOp>(op.body);
            encode_ref(writer, body.object);
            encode(writer, body.attribute);
            break;
        }
        case OperationKind::RemoveObjectAttribute: {
            const auto& body = std::get<RemoveObjectAttributeOp>(op.body);
            encode_ref(writer, body.object);
            writer.string(body.key);
            break;
        }
        case OperationKind::CreatePointer: {
            const auto& body = std::get<CreatePointerOp>(op.body);
            encode_ref(writer, body.from);
            encode_ref(writer, body.to);
            encode_relation_type(writer, body.relation);
            encode_causal_role(writer, body.causal_role);
            writer.f64(body.weight.value);
            writer.string(body.law_domain);
            auto attributes = body.attributes;
            sort_attributes(attributes);
            writer.u64(attributes.size());
            for (const auto& attribute : attributes) {
                encode(writer, attribute);
            }
            break;
        }
        case OperationKind::ExpirePointer: {
            encode_pointer_id(writer, std::get<ExpirePointerOp>(op.body).id);
            break;
        }
        case OperationKind::SetPointerWeight: {
            const auto& body = std::get<SetPointerWeightOp>(op.body);
            encode_pointer_id(writer, body.id);
            writer.f64(body.weight.value);
            break;
        }
        case OperationKind::SetPointerAttribute: {
            const auto& body = std::get<SetPointerAttributeOp>(op.body);
            encode_pointer_id(writer, body.id);
            encode(writer, body.attribute);
            break;
        }
        case OperationKind::RemovePointerAttribute: {
            const auto& body = std::get<RemovePointerAttributeOp>(op.body);
            encode_pointer_id(writer, body.id);
            writer.string(body.key);
            break;
        }
        case OperationKind::EmitEvent:
            encode(writer, std::get<EmitEventOp>(op.body).event);
            break;
        }
    }
}

Delta decode_delta(CanonicalReader& reader) {
    const auto tag = reader.string();
    if (tag != "Delta:v1" && tag != "Delta:v2") {
        throw std::runtime_error("canonical stream has unexpected type tag");
    }
    Delta delta;
    if (tag == "Delta:v2") {
        const auto op_count = checked_count(reader.u64());
        delta.ops.reserve(static_cast<std::size_t>(op_count));
        for (std::uint64_t index = 0; index < op_count; ++index) {
            const auto id = OperationId{reader.u64()};
            const auto kind = static_cast<OperationKind>(reader.u8());
            switch (kind) {
            case OperationKind::CreateObject: {
                ObjectCreate body;
                body.temp_id = TempObjectId{reader.u32()};
                body.name = reader.string();
                body.type = decode_type_id(reader);
                body.existence = decode_existence(reader);
                const auto attribute_count = checked_count(reader.u64());
                body.attributes.reserve(static_cast<std::size_t>(attribute_count));
                for (std::uint64_t attribute = 0; attribute < attribute_count; ++attribute) {
                    body.attributes.push_back(decode_attribute(reader));
                }
                delta.append(make_operation(kind, std::move(body), id));
                break;
            }
            case OperationKind::SetObjectType:
                delta.append(make_operation(kind, SetObjectTypeOp{decode_ref(reader), decode_type_id(reader)}, id));
                break;
            case OperationKind::SetObjectExistence:
                delta.append(make_operation(kind, SetObjectExistenceOp{decode_ref(reader), decode_existence(reader)}, id));
                break;
            case OperationKind::SetObjectAttribute:
                delta.append(make_operation(kind, SetObjectAttributeOp{decode_ref(reader), decode_attribute(reader)}, id));
                break;
            case OperationKind::RemoveObjectAttribute:
                delta.append(make_operation(kind, RemoveObjectAttributeOp{decode_ref(reader), reader.string()}, id));
                break;
            case OperationKind::CreatePointer: {
                PointerCreate body;
                body.from = decode_ref(reader);
                body.to = decode_ref(reader);
                body.relation = decode_relation_type(reader);
                body.causal_role = decode_causal_role(reader);
                body.weight = Weight{reader.f64()};
                body.law_domain = reader.string();
                const auto attribute_count = checked_count(reader.u64());
                body.attributes.reserve(static_cast<std::size_t>(attribute_count));
                for (std::uint64_t attribute = 0; attribute < attribute_count; ++attribute) {
                    body.attributes.push_back(decode_attribute(reader));
                }
                delta.append(make_operation(kind, std::move(body), id));
                break;
            }
            case OperationKind::ExpirePointer:
                delta.append(make_operation(kind, ExpirePointerOp{decode_pointer_id(reader)}, id));
                break;
            case OperationKind::SetPointerWeight:
                delta.append(make_operation(kind, SetPointerWeightOp{decode_pointer_id(reader), Weight{reader.f64()}}, id));
                break;
            case OperationKind::SetPointerAttribute:
                delta.append(make_operation(kind, SetPointerAttributeOp{decode_pointer_id(reader), decode_attribute(reader)}, id));
                break;
            case OperationKind::RemovePointerAttribute:
                delta.append(make_operation(kind, RemovePointerAttributeOp{decode_pointer_id(reader), reader.string()}, id));
                break;
            case OperationKind::EmitEvent:
                delta.append(make_operation(kind, EmitEventOp{decode_trace_event(reader)}, id));
                break;
            }
        }
        return delta;
    }

    const auto create_count = checked_count(reader.u64());
    for (std::uint64_t index = 0; index < create_count; ++index) {
        delta.append_create(ObjectCreate{
            TempObjectId{reader.u32()},
            reader.string(),
            decode_type_id(reader),
            decode_existence(reader),
            {}
        });
    }

    const auto update_count = checked_count(reader.u64());
    for (std::uint64_t index = 0; index < update_count; ++index) {
        delta.append_update(ObjectUpdate{
            decode_ref(reader),
            decode_optional_type(reader),
            decode_optional_existence(reader)
        });
    }

    const auto link_count = checked_count(reader.u64());
    for (std::uint64_t index = 0; index < link_count; ++index) {
        delta.append_link(PointerCreate{
            decode_ref(reader),
            decode_ref(reader),
            decode_relation_type(reader),
            decode_causal_role(reader),
            Weight{reader.f64()},
            reader.string(),
            {}
        });
    }

    const auto unlink_count = checked_count(reader.u64());
    for (std::uint64_t index = 0; index < unlink_count; ++index) {
        delta.append_unlink(PointerRemove{decode_pointer_id(reader)});
    }

    const auto event_count = checked_count(reader.u64());
    for (std::uint64_t index = 0; index < event_count; ++index) {
        delta.append_event(decode_trace_event(reader));
    }
    return delta;
}

void encode(CanonicalWriter& writer, const std::vector<TraceEvent>& events) {
    writer.string("TraceEvents:v1");
    writer.u64(events.size());
    for (const auto& event : events) {
        encode(writer, event);
    }
}

std::vector<TraceEvent> decode_trace_events(CanonicalReader& reader) {
    reader.expect_tag("TraceEvents:v1");
    const auto count = checked_count(reader.u64());
    std::vector<TraceEvent> events;
    events.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0; index < count; ++index) {
        events.push_back(decode_trace_event(reader));
    }
    return events;
}

void encode(CanonicalWriter& writer, const std::vector<LawStatus>& statuses) {
    writer.string("LawStatuses:v1");
    writer.u64(statuses.size());
    for (const auto& status : statuses) {
        encode(writer, status);
    }
}

std::vector<LawStatus> decode_law_statuses(CanonicalReader& reader) {
    reader.expect_tag("LawStatuses:v1");
    const auto count = checked_count(reader.u64());
    std::vector<LawStatus> statuses;
    statuses.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0; index < count; ++index) {
        statuses.push_back(decode_law_status(reader));
    }
    return statuses;
}

void encode(CanonicalWriter& writer, const std::vector<LawViolation>& violations) {
    writer.string("LawViolations:v2");
    writer.u64(violations.size());
    for (const auto& violation : violations) {
        encode(writer, violation);
    }
}

std::vector<LawViolation> decode_law_violations(CanonicalReader& reader) {
    const auto tag = reader.string();
    if (tag != "LawViolations:v1" && tag != "LawViolations:v2") {
        throw std::runtime_error("canonical stream has unexpected type tag");
    }
    const auto count = checked_count(reader.u64());
    std::vector<LawViolation> violations;
    violations.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0; index < count; ++index) {
        if (tag == "LawViolations:v2") {
            violations.push_back(decode_law_violation(reader));
        } else {
            LawViolation violation;
            violation.law = reader.string();
            violation.severity = decode_severity(reader);
            violation.magnitude = reader.f64();
            violation.explanation = reader.string();
            violations.push_back(std::move(violation));
        }
    }
    return violations;
}

void encode_morphism_path(CanonicalWriter& writer, const std::vector<std::string>& path) {
    writer.string("MorphismPath:v1");
    writer.u64(path.size());
    for (const auto& item : path) {
        writer.string(item);
    }
}

std::vector<std::string> decode_morphism_path(CanonicalReader& reader) {
    reader.expect_tag("MorphismPath:v1");
    const auto count = checked_count(reader.u64());
    std::vector<std::string> path;
    path.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0; index < count; ++index) {
        path.push_back(reader.string());
    }
    return path;
}

void encode_commit_record_body(CanonicalWriter& writer, const CommitRecord& record) {
    encode_optional_commit_id(writer, record.parent);
    writer.u64(record.parents.size());
    for (const auto& parent : record.parents) {
        encode_commit_id(writer, parent);
    }
    encode_world_id(writer, record.world);
    encode_branch_id(writer, record.branch);
    writer.string(record.branch_name);
    encode_transaction_id(writer, record.transaction);
    encode_epoch(writer, record.before_epoch);
    encode_epoch(writer, record.after_epoch);
    writer.hash(record.before_hash);
    writer.hash(record.after_hash);
    writer.hash(record.delta_hash);
    writer.hash(record.trace_hash);
    writer.hash(record.law_hash);
    writer.hash(record.violation_hash);
    writer.hash(record.morphism_path_hash);
    writer.hash(record.execution_plan_hash);
    writer.hash(record.read_set_hash);
    writer.hash(record.write_set_hash);
    writer.hash(record.proof_hash);
    writer.u8(record.proof.has_value() ? 1 : 0);
    if (record.proof.has_value()) {
        encode_commit_proof(writer, *record.proof);
    }
    writer.u8(record.accepted ? 1 : 0);
    writer.u8(static_cast<std::uint8_t>(record.origin));
    writer.string(record.label);
}

CommitRecord decode_commit_record_body_v1(CanonicalReader& reader) {
    CommitRecord record;
    record.parent = decode_optional_commit_id(reader);
    const auto parent_count = checked_count(reader.u64());
    record.parents.reserve(static_cast<std::size_t>(parent_count));
    for (std::uint64_t index = 0; index < parent_count; ++index) {
        record.parents.push_back(decode_commit_id(reader));
    }
    record.world = decode_world_id(reader);
    record.branch = decode_branch_id(reader);
    record.branch_name = reader.string();
    record.transaction = decode_transaction_id(reader);
    record.before_epoch = decode_epoch(reader);
    record.after_epoch = decode_epoch(reader);
    record.before_hash = reader.hash();
    record.after_hash = reader.hash();
    record.delta_hash = reader.hash();
    record.trace_hash = reader.hash();
    record.law_hash = reader.hash();
    record.violation_hash = reader.hash();
    record.morphism_path_hash = reader.hash();
    record.accepted = reader.u8() != 0;
    record.origin = static_cast<TransactionOrigin>(reader.u8());
    record.label = reader.string();
    return record;
}

CommitRecord decode_commit_record_body(CanonicalReader& reader) {
    CommitRecord record;
    record.parent = decode_optional_commit_id(reader);
    const auto parent_count = checked_count(reader.u64());
    record.parents.reserve(static_cast<std::size_t>(parent_count));
    for (std::uint64_t index = 0; index < parent_count; ++index) {
        record.parents.push_back(decode_commit_id(reader));
    }
    record.world = decode_world_id(reader);
    record.branch = decode_branch_id(reader);
    record.branch_name = reader.string();
    record.transaction = decode_transaction_id(reader);
    record.before_epoch = decode_epoch(reader);
    record.after_epoch = decode_epoch(reader);
    record.before_hash = reader.hash();
    record.after_hash = reader.hash();
    record.delta_hash = reader.hash();
    record.trace_hash = reader.hash();
    record.law_hash = reader.hash();
    record.violation_hash = reader.hash();
    record.morphism_path_hash = reader.hash();
    record.execution_plan_hash = reader.hash();
    record.read_set_hash = reader.hash();
    record.write_set_hash = reader.hash();
    record.proof_hash = reader.hash();
    if (reader.u8() != 0) {
        record.proof = decode_commit_proof(reader);
    }
    record.accepted = reader.u8() != 0;
    record.origin = static_cast<TransactionOrigin>(reader.u8());
    record.label = reader.string();
    return record;
}

void encode_commit_proof(CanonicalWriter& writer, const CommitProof& proof) {
    writer.hash(proof.before_root);
    writer.hash(proof.operation_root);
    writer.hash(proof.read_set_root);
    writer.hash(proof.write_set_root);
    writer.hash(proof.law_input_root);
    writer.hash(proof.law_output_root);
    writer.hash(proof.after_root);
}

CommitProof decode_commit_proof(CanonicalReader& reader) {
    return CommitProof{
        reader.hash(),
        reader.hash(),
        reader.hash(),
        reader.hash(),
        reader.hash(),
        reader.hash(),
        reader.hash()
    };
}

void encode_commit_identity(CanonicalWriter& writer, const CommitRecord& record) {
    writer.string("StoredCommit:v2");
    encode_commit_record_body(writer, record);
    writer.hash(record.before_hash);
    writer.hash(record.after_hash);
    writer.hash(record.delta_hash);
    writer.hash(record.trace_hash);
    writer.hash(record.law_hash);
    writer.hash(record.violation_hash);
    writer.hash(record.morphism_path_hash);
}

std::vector<std::byte> canonical_encode_morphism_path(const std::vector<std::string>& path) {
    CanonicalWriter writer;
    encode_morphism_path(writer, path);
    return writer.take_bytes();
}

std::vector<std::byte> canonical_encode_commit_identity(const CommitRecord& record) {
    CanonicalWriter writer;
    encode_commit_identity(writer, record);
    return writer.take_bytes();
}

}  // namespace pv
