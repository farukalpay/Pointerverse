// SPDX-License-Identifier: Apache-2.0
#include "pv/core/snapshot.hpp"

#include <functional>

#include "pv/core/delta.hpp"

namespace pv {
namespace {

void hash_combine(std::uint64_t& seed, std::uint64_t value) noexcept {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
}

void hash_string(std::uint64_t& seed, const std::string& value) noexcept {
    hash_combine(seed, std::hash<std::string>{}(value));
}

}  // namespace

bool PointerEdge::active_at(Epoch epoch) const noexcept {
    return born_at <= epoch && (!expires_at.has_value() || epoch < *expires_at);
}

std::string to_string(PointerId id) {
    return id.valid() ? "P" + std::to_string(id.value) : "P<invalid>";
}

bool Delta::empty() const noexcept {
    return creates.empty() && updates.empty() && links.empty() && unlinks.empty() && events.empty();
}

bool WorldSnapshot::contains(ObjectId id) const noexcept {
    return object(id) != nullptr;
}

const ObjectSnapshot* WorldSnapshot::object(ObjectId id) const noexcept {
    for (const auto& candidate : objects) {
        if (candidate.id == id) {
            return &candidate;
        }
    }
    return nullptr;
}

const PointerSnapshot* WorldSnapshot::pointer(PointerId id) const noexcept {
    for (const auto& candidate : pointers) {
        if (candidate.id == id) {
            return &candidate;
        }
    }
    return nullptr;
}

std::string WorldSnapshot::type_name(TypeId type) const {
    if (const auto iter = type_names.find(type.value); iter != type_names.end()) {
        return iter->second;
    }
    return to_string(type);
}

std::string WorldSnapshot::relation_name(RelationType relation) const {
    if (const auto iter = relation_names.find(relation.id); iter != relation_names.end()) {
        return iter->second;
    }
    return to_string(relation);
}

std::uint64_t WorldSnapshot::structural_hash() const noexcept {
    std::uint64_t seed = 1469598103934665603ULL;
    hash_combine(seed, world.value);
    hash_combine(seed, epoch.value);
    hash_string(seed, world_name);

    for (const auto& object_view : objects) {
        hash_combine(seed, object_view.id.index);
        hash_combine(seed, object_view.id.generation);
        hash_string(seed, object_view.name);
        hash_combine(seed, object_view.type.value);
        hash_combine(seed, static_cast<std::uint64_t>(object_view.existence));
        hash_combine(seed, object_view.incoming_count);
        hash_combine(seed, object_view.outgoing_count);
    }

    for (const auto& pointer_view : pointers) {
        hash_combine(seed, pointer_view.id.value);
        hash_combine(seed, pointer_view.from.index);
        hash_combine(seed, pointer_view.to.index);
        hash_combine(seed, pointer_view.relation.id);
        hash_combine(seed, static_cast<std::uint64_t>(pointer_view.causal_role));
        hash_combine(seed, std::hash<double>{}(pointer_view.weight.value));
        hash_combine(seed, pointer_view.born_at.value);
        hash_combine(seed, pointer_view.expires_at ? pointer_view.expires_at->value : 0);
        hash_string(seed, pointer_view.law_domain);
    }

    return seed;
}

}  // namespace pv
