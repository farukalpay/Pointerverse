// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

#include "pv/core/operation.hpp"
#include "pv/core/snapshot.hpp"

namespace pv {

struct Delta {
    std::vector<Operation> ops;

    [[nodiscard]] bool empty() const noexcept;
    void append(Operation operation);
    void append_create(ObjectCreate create);
    void append_update(ObjectUpdate update);
    void append_link(PointerCreate link);
    void append_unlink(PointerRemove unlink);
    void append_event(TraceEvent event);
    void append_set_object_attribute(ObjectRef object, Attribute attribute);
    void append_remove_object_attribute(ObjectRef object, std::string key);
    void append_set_pointer_weight(PointerId pointer, Weight weight);
    void append_set_pointer_attribute(PointerId pointer, Attribute attribute);
    void append_remove_pointer_attribute(PointerId pointer, std::string key);
    void append_intern_type(std::string name, TypeId id = {});
    void append_intern_relation(std::string name, RelationType id = {});
    void append_assert_object(ObjectRef object);
    void append_assert_pointer(PointerId pointer);
    void append_assert_fact(FactId fact);

    [[nodiscard]] std::vector<ObjectCreate> creates_view() const;
    [[nodiscard]] std::vector<ObjectUpdate> updates_view() const;
    [[nodiscard]] std::vector<PointerCreate> links_view() const;
    [[nodiscard]] std::vector<PointerRemove> unlinks_view() const;
    [[nodiscard]] std::vector<TraceEvent> events_view() const;
};

enum class OverlayError {
    DuplicateTempObject,
    DuplicateObjectName,
    InvalidTempObject,
    UpdateMissingObject,
    PointerMissingObject,
    InvalidPointerRemove,
    ConflictingObjectUpdate,
    InvalidPointerRelation,
    InvalidSymbol
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

[[nodiscard]] std::string to_string(OverlayError error);
[[nodiscard]] std::string to_string(DeltaMergeError error);

}  // namespace pv
