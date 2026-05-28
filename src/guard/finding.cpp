// SPDX-License-Identifier: Apache-2.0
#include "pv/guard/finding.hpp"

namespace pv {

std::string_view to_string(FindingSeverity severity) noexcept {
    switch (severity) {
    case FindingSeverity::Info:
        return "info";
    case FindingSeverity::Low:
        return "low";
    case FindingSeverity::Medium:
        return "medium";
    case FindingSeverity::High:
        return "high";
    case FindingSeverity::Critical:
        return "critical";
    }
    return "info";
}

int risk_points(FindingSeverity severity) noexcept {
    switch (severity) {
    case FindingSeverity::Info:
        return 0;
    case FindingSeverity::Low:
        return 3;
    case FindingSeverity::Medium:
        return 10;
    case FindingSeverity::High:
        return 20;
    case FindingSeverity::Critical:
        return 35;
    }
    return 0;
}

}  // namespace pv
