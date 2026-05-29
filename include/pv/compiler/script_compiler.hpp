// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string_view>
#include <vector>

#include "pv/compiler/program_builder.hpp"
#include "pv/core/attribute.hpp"

namespace pv {

class ScriptCompiler {
public:
    [[nodiscard]] Program compile_object(
        const WorldSnapshot& snapshot,
        std::string_view name,
        std::string_view type,
        const std::vector<Attribute>& attributes = {}) const;
    [[nodiscard]] Program compile_link(
        const WorldSnapshot& snapshot,
        std::string_view from,
        std::string_view to,
        std::string_view relation,
        double weight,
        CausalRole role,
        const std::vector<Attribute>& attributes = {}) const;
    [[nodiscard]] Program compile_delta(const WorldSnapshot& snapshot, const Delta& delta) const;
};

}  // namespace pv
