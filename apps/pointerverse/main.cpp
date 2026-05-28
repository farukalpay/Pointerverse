#include <cstdlib>
#include <iostream>

#include <CLI/CLI.hpp>
#include <fmt/format.h>

#include "pointerverse/script.hpp"
#include "pointerverse/world.hpp"

int main(int argc, char** argv) {
    CLI::App app{"Pointerverse categorical reality lab"};
    app.require_subcommand(1);

    std::string script_path;
    auto* lab = app.add_subcommand("lab", "Run a Pointerverse script");
    lab->add_option("script", script_path, "Path to a .pv script")->required();

    auto* repl = app.add_subcommand("repl", "Start the Pointerverse REPL");

    CLI11_PARSE(app, argc, argv);

    pointerverse::World world;
    pointerverse::ScriptEngine engine{world};

    if (lab->parsed()) {
        return engine.run_file(script_path, std::cout) ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (repl->parsed()) {
        std::cout << "Pointerverse lab terminal\n";
        std::cout << "Type help for commands, exit to quit.\n";
        return engine.run_stream(std::cin, std::cout, true) ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
