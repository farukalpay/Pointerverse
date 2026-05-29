// SPDX-License-Identifier: Apache-2.0
#include "commands.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "command_utils.hpp"
#include "pv/projection/entity_projection.hpp"
#include "pv/projection/projection_store.hpp"
#include "pv/projection/relation_projection.hpp"
#include "pv/projection/timeline_projection.hpp"
#include "pv/storage/repository.hpp"

namespace pv::app {
namespace {

class ProjectCommand final : public cli_app::Command {
public:
    void register_with(CLI::App& app) override {
        auto* project = app.add_subcommand("project", "Build deterministic repository projections");
        project->require_subcommand(1);
        set_root(project);

        timeline_ = project->add_subcommand("timeline", "Project branch events into a timeline");
        timeline_->add_option("branch", timeline_branch_, "Branch name")->required();
        timeline_->add_option("--store", store_path_, "Repository path")->default_val(".pvstore");

        entities_ = project->add_subcommand("entities", "Project branch entities");
        entities_->add_option("branch", entities_branch_, "Branch name")->required();
        entities_->add_option("--store", store_path_, "Repository path")->default_val(".pvstore");

        relations_ = project->add_subcommand("relations", "Project branch relations");
        relations_->add_option("branch", relations_branch_, "Branch name")->required();
        relations_->add_option("--store", store_path_, "Repository path")->default_val(".pvstore");
    }

    int run() override {
        if (timeline_->parsed()) {
            return run_checked([&] {
                auto repository = Repository::open(store_path_);
                ProjectionStore store{repository};
                std::cout << render_timeline_projection_text(
                    timeline_branch_,
                    TimelineProjection{}.project(store, timeline_branch_));
                return EXIT_SUCCESS;
            });
        }
        if (entities_->parsed()) {
            return run_checked([&] {
                auto repository = Repository::open(store_path_);
                ProjectionStore store{repository};
                std::cout << render_entity_projection_text(
                    entities_branch_,
                    EntityProjection{}.project(store, entities_branch_));
                return EXIT_SUCCESS;
            });
        }
        if (relations_->parsed()) {
            return run_checked([&] {
                auto repository = Repository::open(store_path_);
                ProjectionStore store{repository};
                std::cout << render_relation_projection_text(
                    relations_branch_,
                    RelationProjection{}.project(store, relations_branch_));
                return EXIT_SUCCESS;
            });
        }
        return EXIT_SUCCESS;
    }

private:
    CLI::App* timeline_{nullptr};
    CLI::App* entities_{nullptr};
    CLI::App* relations_{nullptr};
    std::string store_path_{".pvstore"};
    std::string timeline_branch_;
    std::string entities_branch_;
    std::string relations_branch_;
};

}  // namespace

std::unique_ptr<cli_app::Command> make_project_command() {
    return std::make_unique<ProjectCommand>();
}

}  // namespace pv::app
