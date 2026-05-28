// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "pv/core/delta.hpp"
#include "pv/core/snapshot.hpp"

namespace pv {

using LawId = std::string;

enum class Severity {
    Info,
    Warning,
    Error,
    Fatal
};

struct LawCheckContext {
    const WorldSnapshot& before;
    const Delta& delta;
    const WorldSnapshot& after;
};

struct LawViolation {
    LawId law;
    Severity severity{Severity::Error};
    double magnitude{0.0};
    std::string explanation;
};

struct LawStatus {
    LawId law;
    bool passed{true};
    Severity severity{Severity::Info};
    double magnitude{0.0};
    std::string explanation;
};

class Law {
public:
    virtual ~Law() = default;

    [[nodiscard]] virtual std::string_view name() const = 0;
    [[nodiscard]] virtual std::vector<LawViolation> check(const LawCheckContext& ctx) const = 0;
};

[[nodiscard]] std::string to_string(Severity severity);

}  // namespace pv
