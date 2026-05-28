#pragma once

#include <iosfwd>
#include <string>

#include "pointerverse/world.hpp"

namespace pointerverse {

class ScriptEngine {
public:
    explicit ScriptEngine(World& world);

    bool run_stream(std::istream& input, std::ostream& output, bool interactive = false);
    bool run_file(const std::string& path, std::ostream& output);
    bool execute_line(const std::string& line, std::ostream& output);

private:
    World& world_;
};

}  // namespace pointerverse
