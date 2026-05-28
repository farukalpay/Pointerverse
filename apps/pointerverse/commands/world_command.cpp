// SPDX-License-Identifier: Apache-2.0
#include "commands.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "command_utils.hpp"
#include "pack_runner.hpp"
#include "pv/cli/script.hpp"
#include "pv/core/world.hpp"

namespace pv::app {
namespace {

class WorldCommand final : public cli_app::Command {
public:
    void register_with(CLI::App& app) override {
        auto* world = app.add_subcommand("world", "Build and inspect graph worlds");
        world->require_subcommand(1);
        set_root(world);

        run_ = world->add_subcommand("run", "Run a Pointerverse world script");
        run_->add_option("script", script_path_, "Path to a .pv script")->required();

        repl_ = world->add_subcommand("repl", "Start the Pointerverse world terminal");

        demo_ = world->add_subcommand("demo", "Run a demo pack through the world surface");
        demo_->add_option("pack", demo_pack_, "Pack id")->default_val("city");
    }

    int run() override {
        if (run_->parsed()) {
            return run_checked([&] {
                World world;
                cli::ScriptEngine engine{world};
                return engine.run_file(script_path_, std::cout) ? EXIT_SUCCESS : EXIT_FAILURE;
            });
        }
        if (repl_->parsed()) {
            return run_checked([&] {
                World world;
                cli::ScriptEngine engine{world};
                std::cout << "Pointerverse world terminal\n";
                std::cout << "Type help for commands, exit to quit.\n";
                return engine.run_stream(std::cin, std::cout, true) ? EXIT_SUCCESS : EXIT_FAILURE;
            });
        }
        if (demo_->parsed()) {
            return run_checked([&] {
                return run_pack(demo_pack_);
            });
        }
        return EXIT_SUCCESS;
    }

private:
    CLI::App* run_{nullptr};
    CLI::App* repl_{nullptr};
    CLI::App* demo_{nullptr};
    std::string script_path_;
    std::string demo_pack_{"city"};
};

}  // namespace

std::unique_ptr<cli_app::Command> make_world_command() {
    return std::make_unique<WorldCommand>();
}

}  // namespace pv::app
