// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "pv/core/delta.hpp"
#include "pv/core/snapshot.hpp"

namespace pv {

class WorldIndex {
public:
    void rebuild(const WorldSnapshot& snapshot);
    void apply(const Delta& delta, const WorldSnapshot& snapshot);

    [[nodiscard]] std::span<const ObjectId> objects_by_type(TypeId type) const;
    [[nodiscard]] std::optional<ObjectId> object_by_name(std::string_view name) const;
    [[nodiscard]] std::span<const PointerId> outgoing(ObjectId object) const;
    [[nodiscard]] std::span<const PointerId> incoming(ObjectId object) const;
    [[nodiscard]] std::span<const PointerId> relation(RelationType relation) const;
    [[nodiscard]] std::span<const ObjectId> objects_with_attribute(std::string_view key) const;

private:
    using ObjectKey = std::pair<ObjectIndex, Generation>;

    [[nodiscard]] static ObjectKey key(ObjectId id) noexcept;
    [[nodiscard]] static std::span<const ObjectId> empty_objects() noexcept;
    [[nodiscard]] static std::span<const PointerId> empty_pointers() noexcept;

    std::map<std::uint32_t, std::vector<ObjectId>> objects_by_type_;
    std::unordered_map<std::string, ObjectId> object_by_name_;
    std::map<ObjectKey, std::vector<PointerId>> outgoing_;
    std::map<ObjectKey, std::vector<PointerId>> incoming_;
    std::map<std::uint32_t, std::vector<PointerId>> relation_;
    std::map<std::string, std::vector<ObjectId>> objects_with_attribute_;
};

}  // namespace pv
