// SPDX-License-Identifier: Apache-2.0
#include "commands.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "command_utils.hpp"
#include "pv/decision/decision_report.hpp"
#include "pv/decision/signal_model.hpp"
#include "pv/projection/entity_projection.hpp"
#include "pv/projection/projection_store.hpp"
#include "pv/projection/relation_projection.hpp"
#include "pv/storage/repository.hpp"

namespace pv::app {
namespace {

class DecideCommand final : public cli_app::Command {
public:
    void register_with(CLI::App& app) override {
        auto* decide = app.add_subcommand("decide", "Generate evidence-backed decisions from projections");
        decide->require_subcommand(1);
        set_root(decide);

        report_ = decide->add_subcommand("report", "Render a decision report for a branch");
        report_->add_option("branch", branch_, "Branch name")->required();
        report_->add_option("--store", store_path_, "Repository path")->default_val(".pvstore");
    }

    int run() override {
        if (report_->parsed()) {
            return run_checked([&] {
                auto repository = Repository::open(store_path_);
                ProjectionStore store{repository};
                const auto entities = EntityProjection{}.project(store, branch_);
                const auto relations = RelationProjection{}.project(store, branch_);
                const auto report = SignalModel{}.report(branch_, entities, relations);
                std::cout << render_decision_report_text(report);
                return EXIT_SUCCESS;
            });
        }
        return EXIT_SUCCESS;
    }

private:
    CLI::App* report_{nullptr};
    std::string store_path_{".pvstore"};
    std::string branch_;
};

}  // namespace

std::unique_ptr<cli_app::Command> make_decide_command() {
    return std::make_unique<DecideCommand>();
}

}  // namespace pv::app
