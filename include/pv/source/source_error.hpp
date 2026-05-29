// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <string>

namespace pv {

struct SourceError {
    std::size_t line{0};
    std::string event_id;
    std::string message;
};

}  // namespace pv
