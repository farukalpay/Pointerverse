// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <span>
#include <vector>

#include "pv/core/fact.hpp"
#include "pv/core/snapshot.hpp"

namespace pv {

class FactStore {
public:
    [[nodiscard]] static FactStore from_snapshot(const WorldSnapshot& snapshot);

    [[nodiscard]] std::span<const Fact> facts() const noexcept;
    [[nodiscard]] const Fact* find(FactId id) const noexcept;

private:
    std::vector<Fact> facts_;
};

}  // namespace pv
