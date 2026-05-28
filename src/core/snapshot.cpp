// SPDX-License-Identifier: Apache-2.0
#include "pv/core/snapshot.hpp"

#include <algorithm>
#include <fmt/format.h>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#include <utility>

#include "pv/core/delta.hpp"

namespace pv {
namespace {

void hash_combine(std::uint64_t& seed, std::uint64_t value) noexcept {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
}

void hash_string(std::uint64_t& seed, const std::string& value) noexcept {
    hash_combine(seed, std::hash<std::string>{}(value));
}

Epoch next_epoch(const WorldSnapshot& snapshot) noexcept {
    return Epoch{snapshot.epoch.value + 1};
}

std::uint32_t next_object_index(const WorldSnapshot& snapshot) noexcept {
    std::uint32_t next = 0;
    for (const auto& object : snapshot.objects) {
        next = std::max(next, object.id.index + 1);
    }
    return next;
}

std::uint64_t next_pointer_value(const WorldSnapshot& snapshot) noexcept {
    std::uint64_t next = 1;
    for (const auto& pointer : snapshot.pointers) {
        next = std::max(next, pointer.id.value + 1);
    }
    return next;
}

std::string ref_key(const ObjectRef& ref) {
    return std::visit(
        [](const auto& value) {
            return to_string(value);
        },
        ref);
}

std::optional<ObjectId> resolve_ref(
    const WorldSnapshot& snapshot,
    const std::map<std::uint32_t, ObjectId>& temps,
    const ObjectRef& ref) {
    if (const auto* id = std::get_if<ObjectId>(&ref)) {
        return snapshot.contains(*id) ? std::optional<ObjectId>{*id} : std::nullopt;
    }

    const auto temp = std::get<TempObjectId>(ref);
    if (!temp.valid()) {
        return std::nullopt;
    }
    const auto iter = temps.find(temp.value);
    if (iter == temps.end()) {
        return std::nullopt;
    }
    return iter->second;
}

ObjectSnapshot* mutable_object(WorldSnapshot& snapshot, ObjectId id) noexcept {
    for (auto& object : snapshot.objects) {
        if (object.id == id) {
            return &object;
        }
    }
    return nullptr;
}

PointerSnapshot* mutable_pointer(WorldSnapshot& snapshot, PointerId id) noexcept {
    for (auto& pointer : snapshot.pointers) {
        if (pointer.id == id) {
            return &pointer;
        }
    }
    return nullptr;
}

bool active_at(const PointerSnapshot& pointer, Epoch epoch) noexcept {
    return pointer.born_at <= epoch && (!pointer.expires_at.has_value() || epoch < *pointer.expires_at);
}

void recompute_counts(WorldSnapshot& snapshot) {
    for (auto& object : snapshot.objects) {
        object.incoming_count = 0;
        object.outgoing_count = 0;
    }
    for (const auto& pointer : snapshot.pointers) {
        if (!active_at(pointer, snapshot.epoch)) {
            continue;
        }
        if (auto* from = mutable_object(snapshot, pointer.from)) {
            from->outgoing_count += 1;
        }
        if (auto* to = mutable_object(snapshot, pointer.to)) {
            to->incoming_count += 1;
        }
    }
}

std::expected<void, DeltaMergeError> reject_internal_type_conflicts(const Delta& delta) {
    std::unordered_map<std::string, TypeId> seen;
    for (const auto& update : delta.updates) {
        if (!update.type.has_value()) {
            continue;
        }
        const auto key = ref_key(update.object);
        if (const auto iter = seen.find(key); iter != seen.end() && iter->second != *update.type) {
            return std::unexpected(DeltaMergeError::ConflictingObjectUpdate);
        }
        seen.emplace(key, *update.type);
    }
    return {};
}

std::map<ObjectId, TempObjectId, bool (*)(ObjectId, ObjectId)> synthetic_temp_map(
    const WorldSnapshot& base,
    const Delta& delta) {
    auto less = [](ObjectId left, ObjectId right) {
        if (left.index != right.index) {
            return left.index < right.index;
        }
        return left.generation < right.generation;
    };
    std::map<ObjectId, TempObjectId, bool (*)(ObjectId, ObjectId)> out{less};

    auto next_index = next_object_index(base);
    for (const auto& create : delta.creates) {
        out.emplace(ObjectId{next_index++, 1}, create.temp_id);
    }
    return out;
}

TempObjectId next_temp_id(const Delta& first, const Delta& second) noexcept {
    std::uint32_t next = 1;
    for (const auto& create : first.creates) {
        next = std::max(next, create.temp_id.value + 1);
    }
    for (const auto& create : second.creates) {
        next = std::max(next, create.temp_id.value + 1);
    }
    return TempObjectId{next};
}

Delta compress_updates(Delta delta) {
    std::vector<ObjectUpdate> compressed;
    std::unordered_map<std::string, std::size_t> indexes;

    for (const auto& update : delta.updates) {
        const auto key = ref_key(update.object);
        const auto [iter, inserted] = indexes.emplace(key, compressed.size());
        if (inserted) {
            compressed.push_back(update);
            continue;
        }

        auto& existing = compressed[iter->second];
        if (update.type.has_value()) {
            existing.type = update.type;
        }
        if (update.existence.has_value()) {
            existing.existence = update.existence;
        }
    }

    delta.updates = std::move(compressed);
    return delta;
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

SnapshotOverlay::SnapshotOverlay(const WorldSnapshot& base) : base_(base) {}

std::expected<WorldSnapshot, OverlayError> SnapshotOverlay::apply(const Delta& delta) const {
    WorldSnapshot out = base_;
    out.epoch = next_epoch(base_);

    std::set<std::uint32_t> seen_temps;
    std::set<std::string> names;
    for (const auto& object : out.objects) {
        names.insert(object.name);
    }

    std::map<std::uint32_t, ObjectId> temps;
    auto next_index = next_object_index(out);
    for (const auto& create : delta.creates) {
        if (!create.temp_id.valid()) {
            return std::unexpected(OverlayError::InvalidTempObject);
        }
        if (!seen_temps.insert(create.temp_id.value).second) {
            return std::unexpected(OverlayError::DuplicateTempObject);
        }
        if (!names.insert(create.name).second) {
            return std::unexpected(OverlayError::DuplicateObjectName);
        }
        if (!create.type.valid()) {
            return std::unexpected(OverlayError::InvalidTempObject);
        }

        ObjectSnapshot object;
        object.id = ObjectId{next_index++, 1};
        object.name = create.name;
        object.type = create.type;
        object.existence = create.existence;
        out.objects.push_back(object);
        temps.emplace(create.temp_id.value, object.id);
    }

    for (const auto& update : delta.updates) {
        const auto id = resolve_ref(out, temps, update.object);
        if (!id.has_value()) {
            return std::unexpected(OverlayError::UpdateMissingObject);
        }
        auto* object = mutable_object(out, *id);
        if (object == nullptr) {
            return std::unexpected(OverlayError::UpdateMissingObject);
        }
        if (update.type.has_value()) {
            object->type = *update.type;
        }
        if (update.existence.has_value()) {
            object->existence = *update.existence;
        }
    }

    auto next_pointer = next_pointer_value(out);
    for (const auto& link : delta.links) {
        if (!link.relation.valid()) {
            return std::unexpected(OverlayError::InvalidPointerRelation);
        }
        const auto from = resolve_ref(out, temps, link.from);
        const auto to = resolve_ref(out, temps, link.to);
        if (!from.has_value() || !to.has_value()) {
            return std::unexpected(OverlayError::PointerMissingObject);
        }
        out.pointers.push_back(PointerSnapshot{
            PointerId{next_pointer++},
            *from,
            *to,
            link.relation,
            link.causal_role,
            link.weight,
            out.epoch,
            std::nullopt,
            link.law_domain.empty() ? "core" : link.law_domain
        });
    }

    for (const auto& unlink : delta.unlinks) {
        auto* pointer = mutable_pointer(out, unlink.id);
        if (pointer == nullptr) {
            return std::unexpected(OverlayError::InvalidPointerRemove);
        }
        pointer->expires_at = out.epoch;
    }

    recompute_counts(out);
    return out;
}

std::expected<Delta, DeltaMergeError>
merge_sequential(const WorldSnapshot& base, const Delta& first, const Delta& second) {
    if (!reject_internal_type_conflicts(first).has_value() || !reject_internal_type_conflicts(second).has_value()) {
        return std::unexpected(DeltaMergeError::ConflictingObjectUpdate);
    }

    const auto mid = SnapshotOverlay{base}.apply(first);
    if (!mid.has_value()) {
        return std::unexpected(DeltaMergeError::OverlayRejected);
    }
    if (!SnapshotOverlay{*mid}.apply(second).has_value()) {
        return std::unexpected(DeltaMergeError::OverlayRejected);
    }

    Delta normalized_second;
    std::set<std::uint32_t> first_temps;
    for (const auto& create : first.creates) {
        first_temps.insert(create.temp_id.value);
    }

    auto next_temp = next_temp_id(first, second);
    std::unordered_map<std::uint32_t, TempObjectId> second_temp_remap;
    for (const auto& create : second.creates) {
        if (!create.temp_id.valid()) {
            return std::unexpected(DeltaMergeError::DuplicateTempObject);
        }
        auto remapped = create.temp_id;
        if (first_temps.contains(create.temp_id.value)) {
            remapped = next_temp;
            next_temp.value += 1;
        }
        second_temp_remap.emplace(create.temp_id.value, remapped);

        auto out_create = create;
        out_create.temp_id = remapped;
        normalized_second.creates.push_back(std::move(out_create));
    }

    const auto first_synthetic = synthetic_temp_map(base, first);
    auto normalize_ref = [&](const ObjectRef& ref) -> std::expected<ObjectRef, DeltaMergeError> {
        if (const auto* temp = std::get_if<TempObjectId>(&ref)) {
            if (!temp->valid()) {
                return std::unexpected(DeltaMergeError::UpdateMissingObject);
            }
            if (const auto iter = second_temp_remap.find(temp->value); iter != second_temp_remap.end()) {
                return ObjectRef{iter->second};
            }
            if (first_temps.contains(temp->value)) {
                return ObjectRef{*temp};
            }
            return std::unexpected(DeltaMergeError::UpdateMissingObject);
        }

        const auto id = std::get<ObjectId>(ref);
        if (base.contains(id)) {
            return ObjectRef{id};
        }
        if (const auto iter = first_synthetic.find(id); iter != first_synthetic.end()) {
            return ObjectRef{iter->second};
        }
        return std::unexpected(DeltaMergeError::UpdateMissingObject);
    };

    for (const auto& update : second.updates) {
        auto object = normalize_ref(update.object);
        if (!object.has_value()) {
            return std::unexpected(DeltaMergeError::UpdateMissingObject);
        }
        normalized_second.updates.push_back(ObjectUpdate{*object, update.type, update.existence});
    }

    for (const auto& link : second.links) {
        auto from = normalize_ref(link.from);
        auto to = normalize_ref(link.to);
        if (!from.has_value() || !to.has_value()) {
            return std::unexpected(DeltaMergeError::PointerMissingObject);
        }
        normalized_second.links.push_back(PointerCreate{
            *from,
            *to,
            link.relation,
            link.causal_role,
            link.weight,
            link.law_domain
        });
    }

    normalized_second.unlinks = second.unlinks;
    normalized_second.events = second.events;

    Delta out;
    out.creates = first.creates;
    out.creates.insert(out.creates.end(), normalized_second.creates.begin(), normalized_second.creates.end());
    out.updates = first.updates;
    out.updates.insert(out.updates.end(), normalized_second.updates.begin(), normalized_second.updates.end());
    out.links = first.links;
    out.links.insert(out.links.end(), normalized_second.links.begin(), normalized_second.links.end());
    out.unlinks = first.unlinks;
    out.unlinks.insert(out.unlinks.end(), normalized_second.unlinks.begin(), normalized_second.unlinks.end());
    out.events = first.events;
    out.events.insert(out.events.end(), normalized_second.events.begin(), normalized_second.events.end());
    out = compress_updates(std::move(out));

    if (!SnapshotOverlay{base}.apply(out).has_value()) {
        return std::unexpected(DeltaMergeError::OverlayRejected);
    }
    return out;
}

std::expected<Delta, DeltaMergeError>
merge_sequential(const Delta& first, const Delta& second) {
    WorldSnapshot base;
    base.world_name = "overlay";
    return merge_sequential(base, first, second);
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

std::string to_string(TempObjectId id) {
    return id.valid() ? fmt::format("T{}", id.value) : "T<invalid>";
}

std::string to_string(const ObjectRef& ref) {
    return std::visit(
        [](const auto& value) {
            return to_string(value);
        },
        ref);
}

std::string to_string(OverlayError error) {
    switch (error) {
    case OverlayError::DuplicateTempObject:
        return "DuplicateTempObject";
    case OverlayError::DuplicateObjectName:
        return "DuplicateObjectName";
    case OverlayError::InvalidTempObject:
        return "InvalidTempObject";
    case OverlayError::UpdateMissingObject:
        return "UpdateMissingObject";
    case OverlayError::PointerMissingObject:
        return "PointerMissingObject";
    case OverlayError::InvalidPointerRemove:
        return "InvalidPointerRemove";
    case OverlayError::ConflictingObjectUpdate:
        return "ConflictingObjectUpdate";
    case OverlayError::InvalidPointerRelation:
        return "InvalidPointerRelation";
    }
    return "InvalidTempObject";
}

std::string to_string(DeltaMergeError error) {
    switch (error) {
    case DeltaMergeError::OverlayRejected:
        return "OverlayRejected";
    case DeltaMergeError::DuplicateTempObject:
        return "DuplicateTempObject";
    case DeltaMergeError::ConflictingObjectUpdate:
        return "ConflictingObjectUpdate";
    case DeltaMergeError::UpdateMissingObject:
        return "UpdateMissingObject";
    case DeltaMergeError::PointerMissingObject:
        return "PointerMissingObject";
    case DeltaMergeError::InvalidPointerRemove:
        return "InvalidPointerRemove";
    case DeltaMergeError::InvalidPointerRelation:
        return "InvalidPointerRelation";
    }
    return "OverlayRejected";
}

}  // namespace pv
