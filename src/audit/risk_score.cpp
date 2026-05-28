// SPDX-License-Identifier: Apache-2.0
#include "pv/audit/risk_score.hpp"

namespace pv {

int risk_points(Severity severity) noexcept {
    switch (severity) {
    case Severity::Info:
        return 0;
    case Severity::Warning:
        return 1;
    case Severity::Error:
        return 5;
    case Severity::Fatal:
        return 10;
    }
    return 0;
}

int risk_points(std::string_view severity) noexcept {
    if (severity == "warning") {
        return 1;
    }
    if (severity == "error") {
        return 5;
    }
    if (severity == "fatal") {
        return 10;
    }
    return 0;
}

}  // namespace pv
