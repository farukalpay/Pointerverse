// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "pv/source/source_error.hpp"
#include "pv/source/source_event.hpp"

namespace pv {

struct SourceBatch {
    std::vector<SourceEvent> events;
    std::vector<SourceError> errors;
};

}  // namespace pv
