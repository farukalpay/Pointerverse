// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <vector>

#include "pv/cli_app/command.hpp"

namespace pv::cli_app {

class CommandRegistry {
public:
    void add(std::unique_ptr<Command> command);
    void register_with(CLI::App& app);
    [[nodiscard]] int run() const;

private:
    std::vector<std::unique_ptr<Command>> commands_;
};

}  // namespace pv::cli_app
