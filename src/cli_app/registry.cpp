// SPDX-License-Identifier: Apache-2.0
#include "pv/cli_app/registry.hpp"

#include <cstdlib>

namespace pv::cli_app {

void CommandRegistry::add(std::unique_ptr<Command> command) {
    commands_.push_back(std::move(command));
}

void CommandRegistry::register_with(CLI::App& app) {
    for (auto& command : commands_) {
        command->register_with(app);
    }
}

int CommandRegistry::run() const {
    for (const auto& command : commands_) {
        if (command->selected()) {
            return command->run();
        }
    }
    return EXIT_SUCCESS;
}

}  // namespace pv::cli_app
