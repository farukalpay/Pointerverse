// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "pv/core/object.hpp"
#include "pv/core/pointer.hpp"
#include "pv/trace/event.hpp"

namespace pv {

struct ObjectCreate {
    std::string name;
    TypeId type;
    ExistenceState existence{ExistenceState::Alive};
};

struct ObjectUpdate {
    ObjectId id;
    std::optional<TypeId> type;
    std::optional<ExistenceState> existence;
};

struct PointerCreate {
    ObjectId from;
    ObjectId to;
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

}  // namespace pv
