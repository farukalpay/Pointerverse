// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <iosfwd>
#include <memory>
#include <string>
#include <unordered_map>

#include "pv/category/morphism.hpp"
#include "pv/core/world.hpp"

namespace pv::cli {

class ScriptEngine {
public:
    explicit ScriptEngine(World& world);

    bool run_stream(std::istream& input, std::ostream& output, bool interactive = false);
    bool run_file(const std::string& path, std::ostream& output);
    bool execute_line(const std::string& line, std::ostream& output);

private:
    World& world_;
    Verifier verifier_;
    std::unordered_map<std::string, std::shared_ptr<Morphism>> morphisms_;
};

}  // namespace pv::cli
