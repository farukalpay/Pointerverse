// SPDX-License-Identifier: Apache-2.0
#include <cstdlib>
#include <exception>
#include <iostream>

#include <CLI/CLI.hpp>
#include <fmt/format.h>

#include "commands/commands.hpp"
#include "pv/cli_app/registry.hpp"

int main(int argc, char** argv) {
    CLI::App app{"Pointerverse verifiable graph-world engine"};
    app.require_subcommand(1);

    pv::cli_app::CommandRegistry registry;
    pv::app::register_pointerverse_commands(registry);
    registry.register_with(app);

    try {
        app.parse(argc, argv);
        return registry.run();
    } catch (const CLI::ParseError& error) {
        return app.exit(error);
    } catch (const std::exception& error) {
        std::cerr << fmt::format("error: {}\n", error.what());
        return EXIT_FAILURE;
    }
}
