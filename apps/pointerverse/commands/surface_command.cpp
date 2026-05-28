// SPDX-License-Identifier: Apache-2.0
#include "commands.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <fmt/format.h>

#include "command_utils.hpp"
#include "pv/surface/registry.hpp"

namespace pv::app {
namespace {

void print_surface(const SurfaceManifest& surface) {
    std::cout << fmt::format("{}\n", surface.title);
    std::cout << std::string(surface.title.size(), '-') << "\n";
    std::cout << surface.one_liner << "\n";
    if (!surface.commands.empty()) {
        std::cout << "\ncommands\n";
        for (const auto& command : surface.commands) {
            std::cout << fmt::format("  {}\n", command);
        }
    }
    if (!surface.examples.empty()) {
        std::cout << "\nexamples\n";
        for (const auto& example : surface.examples) {
            std::cout << fmt::format("  {}\n", example);
        }
    }
}

class SurfaceCommand final : public cli_app::Command {
public:
    void register_with(CLI::App& app) override {
        surfaces_ = app.add_subcommand("surfaces", "List Pointerverse product surfaces");
        set_root(surfaces_);

        surface_ = app.add_subcommand("surface", "Inspect a Pointerverse product surface");
        surface_->require_subcommand(1);
        show_ = surface_->add_subcommand("show", "Show a surface manifest");
        show_->add_option("id", surface_id_, "Surface id")->required();
    }

    [[nodiscard]] bool selected() const noexcept override {
        return (surfaces_ != nullptr && surfaces_->parsed()) || (surface_ != nullptr && surface_->parsed());
    }

    int run() override {
        if (surfaces_->parsed()) {
            return run_checked([&] {
                for (const auto& surface : built_in_surfaces()) {
                    std::cout << fmt::format("{:<10} {}\n", surface.id, surface.one_liner);
                }
                return EXIT_SUCCESS;
            });
        }
        if (surface_->parsed() && show_->parsed()) {
            return run_checked([&] {
                const auto surface = find_surface(surface_id_);
                if (!surface.has_value()) {
                    throw std::runtime_error(fmt::format("unknown surface '{}'", surface_id_));
                }
                print_surface(*surface);
                return EXIT_SUCCESS;
            });
        }
        return EXIT_SUCCESS;
    }

private:
    CLI::App* surfaces_{nullptr};
    CLI::App* surface_{nullptr};
    CLI::App* show_{nullptr};
    std::string surface_id_;
};

}  // namespace

std::unique_ptr<cli_app::Command> make_surface_command() {
    return std::make_unique<SurfaceCommand>();
}

}  // namespace pv::app
