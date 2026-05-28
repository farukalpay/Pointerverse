// SPDX-License-Identifier: Apache-2.0
#include "pv/core/object.hpp"

#include <fmt/format.h>
#include <stdexcept>

namespace pv {

ObjectId ObjectArena::create(std::string name, TypeId type, ExistenceState existence) {
    if (name.empty()) {
        throw std::invalid_argument("object name cannot be empty");
    }
    if (!type.valid()) {
        throw std::invalid_argument("object type must be valid");
    }

    Object object;
    object.id = ObjectId{static_cast<ObjectIndex>(objects_.size()), 1};
    object.name = std::move(name);
    object.type = type;
    object.existence = existence;

    const auto id = object.id;
    objects_.push_back(std::move(object));
    return id;
}

bool ObjectArena::contains(ObjectId id) const noexcept {
    return id.valid_token()
        && id.index < objects_.size()
        && objects_[id.index].id == id;
}

const Object& ObjectArena::get(ObjectId id) const {
    if (!contains(id)) {
        throw std::out_of_range(fmt::format("unknown object {}", to_string(id)));
    }
    return objects_[id.index];
}

Object& ObjectArena::get(ObjectId id) {
    if (!contains(id)) {
        throw std::out_of_range(fmt::format("unknown object {}", to_string(id)));
    }
    return objects_[id.index];
}

const std::vector<Object>& ObjectArena::objects() const noexcept {
    return objects_;
}

std::size_t ObjectArena::size() const noexcept {
    return objects_.size();
}

void ObjectArena::set_existence(ObjectId id, ExistenceState state) {
    get(id).existence = state;
}

void ObjectArena::set_type(ObjectId id, TypeId type) {
    if (!type.valid()) {
        throw std::invalid_argument("object type must be valid");
    }
    get(id).type = type;
}

void ObjectArena::restore(std::vector<Object> objects) {
    objects_ = std::move(objects);
}

std::string to_string(ExistenceState state) {
    switch (state) {
    case ExistenceState::Alive:
        return "Alive";
    case ExistenceState::Dormant:
        return "Dormant";
    case ExistenceState::Collapsed:
        return "Collapsed";
    case ExistenceState::Tombstoned:
        return "Tombstoned";
    }
    return "Alive";
}

ExistenceState existence_state_from_string(std::string_view value) {
    if (value == "Dormant" || value == "dormant") {
        return ExistenceState::Dormant;
    }
    if (value == "Collapsed" || value == "collapsed") {
        return ExistenceState::Collapsed;
    }
    if (value == "Tombstoned" || value == "tombstoned") {
        return ExistenceState::Tombstoned;
    }
    return ExistenceState::Alive;
}

}  // namespace pv
