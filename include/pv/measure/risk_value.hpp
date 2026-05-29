// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace pv {

struct RiskVector {
    std::uint64_t structural{0};
    std::uint64_t law_distance{0};
    std::uint64_t repair_distance{0};
    std::uint64_t surprise{0};

    friend bool operator==(RiskVector, RiskVector) = default;
};

[[nodiscard]] bool empty(RiskVector value) noexcept;

}  // namespace pv

