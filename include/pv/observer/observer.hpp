// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <string_view>

#include "pv/core/id.hpp"
#include "pv/core/snapshot.hpp"
#include "pv/observer/projection.hpp"

namespace pv {

class Observer {
public:
    explicit Observer(std::string name = "world");

    [[nodiscard]] Projection inspect_object(const WorldSnapshot& snapshot, ObjectId id) const;
    [[nodiscard]] Projection inspect_object(const WorldSnapshot& snapshot, std::string_view name) const;
    [[nodiscard]] Projection inspect_graph(const WorldSnapshot& snapshot) const;

private:
    std::string name_;
};

}  // namespace pv
