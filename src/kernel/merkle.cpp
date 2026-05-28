// SPDX-License-Identifier: Apache-2.0
#include "pv/kernel/merkle.hpp"

#include <algorithm>
#include <set>
#include <string_view>

#include "pv/hash/hasher.hpp"
#include "pv/kernel/fact_store.hpp"
#include "pv/kernel/canonical_codec.hpp"

namespace pv {
namespace {

Hash256 hash_writer(const CanonicalWriter& writer) {
    return sha256(writer.bytes());
}

Hash256 object_hash(const ObjectSnapshot& object) {
    CanonicalWriter writer;
    writer.string("MerkleObject:v1");
    writer.u32(object.id.index);
    writer.u32(object.id.generation);
    writer.string(object.name);
    writer.u32(object.type.value);
    writer.u8(static_cast<std::uint8_t>(object.existence));
    auto attributes = object.attributes;
    sort_attributes(attributes);
    writer.u64(attributes.size());
    for (const auto& attribute : attributes) {
        encode(writer, attribute);
    }
    return hash_writer(writer);
}

Hash256 pointer_hash(const PointerSnapshot& pointer) {
    CanonicalWriter writer;
    writer.string("MerklePointer:v1");
    writer.u64(pointer.id.value);
    writer.u32(pointer.from.index);
    writer.u32(pointer.from.generation);
    writer.u32(pointer.to.index);
    writer.u32(pointer.to.generation);
    writer.u32(pointer.relation.id);
    writer.u8(static_cast<std::uint8_t>(pointer.causal_role));
    writer.f64(pointer.weight.value);
    writer.u64(pointer.born_at.value);
    writer.u8(pointer.expires_at.has_value() ? 1 : 0);
    if (pointer.expires_at.has_value()) {
        writer.u64(pointer.expires_at->value);
    }
    writer.string(pointer.law_domain);
    auto attributes = pointer.attributes;
    sort_attributes(attributes);
    writer.u64(attributes.size());
    for (const auto& attribute : attributes) {
        encode(writer, attribute);
    }
    return hash_writer(writer);
}

Hash256 fact_hash(const Fact& fact) {
    CanonicalWriter writer;
    writer.string("MerkleFact:v1");
    encode(writer, fact);
    return hash_writer(writer);
}

}  // namespace

Hash256 compute_hash_list_root(std::span<const Hash256> hashes, std::string_view label) {
    std::vector<Hash256> sorted{hashes.begin(), hashes.end()};
    std::ranges::sort(sorted, [](Hash256 left, Hash256 right) {
        return to_hex(left) < to_hex(right);
    });

    CanonicalWriter writer;
    writer.string(label);
    writer.u64(sorted.size());
    for (const auto& hash : sorted) {
        writer.hash(hash);
    }
    return hash_writer(writer);
}

Hash256 compute_fact_id_root(std::span<const FactId> facts) {
    std::vector<Hash256> hashes;
    hashes.reserve(facts.size());
    for (const auto& fact : facts) {
        hashes.push_back(fact.value);
    }
    return compute_hash_list_root(hashes, "FactIdRoot:v1");
}

WorldRoot compute_world_root(const WorldSnapshot& snapshot) {
    std::vector<Hash256> object_hashes;
    object_hashes.reserve(snapshot.objects.size());
    for (const auto& object : snapshot.objects) {
        object_hashes.push_back(object_hash(object));
    }

    std::vector<Hash256> pointer_hashes;
    pointer_hashes.reserve(snapshot.pointers.size());
    for (const auto& pointer : snapshot.pointers) {
        pointer_hashes.push_back(pointer_hash(pointer));
    }

    const auto fact_store = FactStore::from_snapshot(snapshot);
    std::vector<Hash256> fact_hashes;
    fact_hashes.reserve(fact_store.facts().size());
    for (const auto& fact : fact_store.facts()) {
        fact_hashes.push_back(fact_hash(fact));
    }

    std::vector<Hash256> type_hashes;
    std::set<std::uint32_t> used_type_ids;
    for (const auto& object : snapshot.objects) {
        used_type_ids.insert(object.type.value);
    }
    type_hashes.reserve(used_type_ids.size());
    for (const auto id : used_type_ids) {
        CanonicalWriter writer;
        writer.string("MerkleType:v1");
        writer.u32(id);
        if (const auto iter = snapshot.type_names.find(id); iter != snapshot.type_names.end()) {
            writer.string(iter->second);
        } else {
            writer.string({});
        }
        type_hashes.push_back(hash_writer(writer));
    }

    std::vector<Hash256> relation_hashes;
    std::set<std::uint32_t> used_relation_ids;
    for (const auto& pointer : snapshot.pointers) {
        used_relation_ids.insert(pointer.relation.id);
    }
    relation_hashes.reserve(used_relation_ids.size());
    for (const auto id : used_relation_ids) {
        CanonicalWriter writer;
        writer.string("MerkleRelation:v1");
        writer.u32(id);
        if (const auto iter = snapshot.relation_names.find(id); iter != snapshot.relation_names.end()) {
            writer.string(iter->second);
        } else {
            writer.string({});
        }
        relation_hashes.push_back(hash_writer(writer));
    }

    WorldRoot out;
    out.object_root = compute_hash_list_root(object_hashes, "ObjectRoot:v1");
    out.pointer_root = compute_hash_list_root(pointer_hashes, "PointerRoot:v1");
    out.fact_root = compute_hash_list_root(fact_hashes, "FactRoot:v1");
    out.type_root = compute_hash_list_root(type_hashes, "TypeRoot:v1");
    out.relation_root = compute_hash_list_root(relation_hashes, "RelationRoot:v1");

    CanonicalWriter writer;
    writer.string("WorldRoot:v1");
    writer.hash(out.object_root);
    writer.hash(out.pointer_root);
    writer.hash(out.fact_root);
    writer.hash(out.type_root);
    writer.hash(out.relation_root);
    writer.u64(snapshot.epoch.value);
    out.root = hash_writer(writer);
    return out;
}

}  // namespace pv
