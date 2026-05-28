// SPDX-License-Identifier: Apache-2.0
#include "commands.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <fmt/format.h>

#include "command_utils.hpp"
#include "pack_runner.hpp"
#include "pv/surface/registry.hpp"

namespace pv::app {
namespace {

class PackCommand final : public cli_app::Command {
public:
    void register_with(CLI::App& app) override {
        packs_ = app.add_subcommand("packs", "List Pointerverse demo packs");
        set_root(packs_);

        pack_ = app.add_subcommand("pack", "Run or inspect Pointerverse demo packs");
        pack_->require_subcommand(1);

        run_ = pack_->add_subcommand("run", "Run a demo pack");
        run_->add_option("id", pack_id_, "Pack id")->required();
        run_->add_option("--packs-root", packs_root_, "Pack root directory");
    }

    [[nodiscard]] bool selected() const noexcept override {
        return (packs_ != nullptr && packs_->parsed()) || (pack_ != nullptr && pack_->parsed());
    }

    int run() override {
        if (packs_->parsed()) {
            return run_checked([&] {
                for (const auto& pack : discover_packs(active_packs_root())) {
                    std::cout << fmt::format("{:<18} {:<12} {}\n", pack.id, pack.surface, pack.title);
                }
                return EXIT_SUCCESS;
            });
        }
        if (run_->parsed()) {
            return run_checked([&] {
                return run_pack(pack_id_, active_packs_root());
            });
        }
        return EXIT_SUCCESS;
    }

private:
    [[nodiscard]] std::filesystem::path active_packs_root() const {
        if (!packs_root_.empty()) {
            return packs_root_;
        }
        return default_packs_root();
    }

    CLI::App* packs_{nullptr};
    CLI::App* pack_{nullptr};
    CLI::App* run_{nullptr};
    std::string pack_id_;
    std::string packs_root_;
};

}  // namespace

std::unique_ptr<cli_app::Command> make_pack_command() {
    return std::make_unique<PackCommand>();
}

}  // namespace pv::app
