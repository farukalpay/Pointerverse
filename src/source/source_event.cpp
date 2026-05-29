// SPDX-License-Identifier: Apache-2.0
#include "pv/source/source_event.hpp"

namespace pv {

bool valid_source_event_key(std::string_view value) noexcept {
    return !value.empty()
        && value.find('\t') == std::string_view::npos
        && value.find('\n') == std::string_view::npos
        && value.find('\r') == std::string_view::npos;
}

}  // namespace pv
