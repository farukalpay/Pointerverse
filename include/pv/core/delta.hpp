// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "pv/core/object.hpp"
#include "pv/core/pointer.hpp"
#include "pv/core/snapshot.hpp"
#include "pv/trace/event.hpp"

namespace pv {

struct TempObjectId {
    std::uint32_t value{0};

    [[nodiscard]] bool valid() const noexcept { return value != 0; }

    friend bool operator==(TempObjectId, TempObjectId) = default;
};

using ObjectRef = std::variant<ObjectId, TempObjectId>;

struct ObjectCreate {
    TempObjectId temp_id;
    std::string name;
    TypeId type;
    ExistenceState existence{ExistenceState::Alive};
};

struct ObjectUpdate {
    ObjectRef object;
    std::optional<TypeId> type;
    std::optional<ExistenceState> existence;
};

struct PointerCreate {
    ObjectRef from;
    ObjectRef to;
    RelationType relation;
    CausalRole causal_role{CausalRole::Structural};
    Weight weight;
    std::string law_domain{"core"};
};

struct PointerRemove {
    PointerId id;
};

struct Delta {
    std::vector<ObjectCreate> creates;
    std::vector<ObjectUpdate> updates;
    std::vector<PointerCreate> links;
    std::vector<PointerRemove> unlinks;
    std::vector<TraceEvent> events;

    [[nodiscard]] bool empty() const noexcept;
};

enum class OverlayError {
    DuplicateTempObject,
    DuplicateObjectName,
    InvalidTempObject,
    UpdateMissingObject,
    PointerMissingObject,
    InvalidPointerRemove,
    ConflictingObjectUpdate,
    InvalidPointerRelation
};

enum class DeltaMergeError {
    OverlayRejected,
    DuplicateTempObject,
    ConflictingObjectUpdate,
    UpdateMissingObject,
    PointerMissingObject,
    InvalidPointerRemove,
    InvalidPointerRelation
};

class SnapshotOverlay {
public:
    explicit SnapshotOverlay(const WorldSnapshot& base);

    [[nodiscard]] std::expected<WorldSnapshot, OverlayError> apply(const Delta& delta) const;

private:
    const WorldSnapshot& base_;
};

[[nodiscard]] std::expected<Delta, DeltaMergeError>
merge_sequential(const WorldSnapshot& base, const Delta& first, const Delta& second);

[[nodiscard]] std::expected<Delta, DeltaMergeError>
merge_sequential(const Delta& first, const Delta& second);

[[nodiscard]] std::string to_string(TempObjectId id);
[[nodiscard]] std::string to_string(const ObjectRef& ref);
[[nodiscard]] std::string to_string(OverlayError error);
[[nodiscard]] std::string to_string(DeltaMergeError error);

}  // namespace pv
