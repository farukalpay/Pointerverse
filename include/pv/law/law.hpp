// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "pv/core/delta.hpp"
#include "pv/core/fact.hpp"
#include "pv/core/snapshot.hpp"

namespace pv {

struct ExecutionPlan;
class FactStore;
class WorldIndex;

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
    const ExecutionPlan* plan{nullptr};
    const WorldIndex* before_index{nullptr};
    const WorldIndex* after_index{nullptr};
    const FactStore* before_facts{nullptr};
    const FactStore* after_facts{nullptr};
};

struct LawViolation {
    LawId law;
    Severity severity{Severity::Error};
    double magnitude{0.0};
    std::string explanation;
    std::vector<FactId> evidence;
    std::vector<ObjectId> objects;
    std::vector<PointerId> pointers;
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
