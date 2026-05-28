// SPDX-License-Identifier: Apache-2.0
#include "pv/core/world_index.hpp"

#include <algorithm>

namespace pv {
namespace {

bool active_at(const PointerSnapshot& pointer, Epoch epoch) noexcept {
    return pointer.born_at <= epoch && (!pointer.expires_at.has_value() || epoch < *pointer.expires_at);
}

void sort_objects(std::vector<ObjectId>& ids) {
    std::ranges::sort(ids, [](ObjectId left, ObjectId right) {
        if (left.index != right.index) {
            return left.index < right.index;
        }
        return left.generation < right.generation;
    });
}

void sort_pointers(std::vector<PointerId>& ids) {
    std::ranges::sort(ids, [](PointerId left, PointerId right) {
        return left.value < right.value;
    });
}

}  // namespace

void WorldIndex::rebuild(const WorldSnapshot& snapshot) {
    objects_by_type_.clear();
    object_by_name_.clear();
    outgoing_.clear();
    incoming_.clear();
    relation_.clear();
    objects_with_attribute_.clear();

    for (const auto& object : snapshot.objects) {
        objects_by_type_[object.type.value].push_back(object.id);
        object_by_name_.emplace(object.name, object.id);
        for (const auto& attribute : object.attributes) {
            objects_with_attribute_[attribute.key].push_back(object.id);
        }
    }

    for (const auto& pointer : snapshot.pointers) {
        if (!active_at(pointer, snapshot.epoch)) {
            continue;
        }
        outgoing_[key(pointer.from)].push_back(pointer.id);
        incoming_[key(pointer.to)].push_back(pointer.id);
        relation_[pointer.relation.id].push_back(pointer.id);
    }

    for (auto& [_, ids] : objects_by_type_) {
        sort_objects(ids);
    }
    for (auto& [_, ids] : outgoing_) {
        sort_pointers(ids);
    }
    for (auto& [_, ids] : incoming_) {
        sort_pointers(ids);
    }
    for (auto& [_, ids] : relation_) {
        sort_pointers(ids);
    }
    for (auto& [_, ids] : objects_with_attribute_) {
        sort_objects(ids);
        ids.erase(std::ranges::unique(ids).begin(), ids.end());
    }
}

void WorldIndex::apply(const Delta&, const WorldSnapshot& snapshot) {
    rebuild(snapshot);
}

std::span<const ObjectId> WorldIndex::objects_by_type(TypeId type) const {
    const auto iter = objects_by_type_.find(type.value);
    return iter == objects_by_type_.end() ? empty_objects() : std::span<const ObjectId>{iter->second};
}

std::optional<ObjectId> WorldIndex::object_by_name(std::string_view name) const {
    const auto iter = object_by_name_.find(std::string{name});
    if (iter == object_by_name_.end()) {
        return std::nullopt;
    }
    return iter->second;
}

std::span<const PointerId> WorldIndex::outgoing(ObjectId object) const {
    const auto iter = outgoing_.find(key(object));
    return iter == outgoing_.end() ? empty_pointers() : std::span<const PointerId>{iter->second};
}

std::span<const PointerId> WorldIndex::incoming(ObjectId object) const {
    const auto iter = incoming_.find(key(object));
    return iter == incoming_.end() ? empty_pointers() : std::span<const PointerId>{iter->second};
}

std::span<const PointerId> WorldIndex::relation(RelationType relation_type) const {
    const auto iter = relation_.find(relation_type.id);
    return iter == relation_.end() ? empty_pointers() : std::span<const PointerId>{iter->second};
}

std::span<const ObjectId> WorldIndex::objects_with_attribute(std::string_view key_name) const {
    const auto iter = objects_with_attribute_.find(std::string{key_name});
    return iter == objects_with_attribute_.end() ? empty_objects() : std::span<const ObjectId>{iter->second};
}

WorldIndex::ObjectKey WorldIndex::key(ObjectId id) noexcept {
    return {id.index, id.generation};
}

std::span<const ObjectId> WorldIndex::empty_objects() noexcept {
    static const std::vector<ObjectId> empty;
    return empty;
}

std::span<const PointerId> WorldIndex::empty_pointers() noexcept {
    static const std::vector<PointerId> empty;
    return empty;
}

}  // namespace pv
