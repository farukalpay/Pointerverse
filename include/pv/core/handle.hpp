// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pv/core/id.hpp"

namespace pv {

template <class T>
struct Handle {
    ObjectId id;

    [[nodiscard]] bool valid_token() const noexcept {
        return id.valid_token();
    }

    friend bool operator==(Handle, Handle) = default;
};

}  // namespace pv
