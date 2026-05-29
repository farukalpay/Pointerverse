// SPDX-License-Identifier: Apache-2.0
#include "commands.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

#include <fmt/format.h>

#include "command_utils.hpp"
#include "pv/breakpoint/breakpoint_finder.hpp"
#include "pv/breakpoint/evidence_chain.hpp"
#include "pv/breakpoint/repair_candidate.hpp"
#include "pv/cli/script.hpp"
#include "pv/measure/breakpoint_measure.hpp"
#include "pv/projection/projection_store.hpp"
#include "pv/storage/integrity.hpp"
#include "pv/storage/repository.hpp"

namespace pv::app {
namespace {

Breakpoint require_breakpoint(
    const ProjectionStore& store,
    std::string_view branch,
    std::string_view breakpoint_id) {
    auto breakpoint = BreakpointFinder{}.find_by_id(store, branch, breakpoint_id);
    if (!breakpoint.has_value()) {
        throw std::invalid_argument(fmt::format("unknown breakpoint '{}'", breakpoint_id));
    }
    return *breakpoint;
}

class BreakpointCommand final : public cli_app::Command {
public:
    void register_with(CLI::App& app) override {
        auto* breakpoint = app.add_subcommand("breakpoint", "Find and repair causal breakpoints");
        breakpoint->require_subcommand(1);
        set_root(breakpoint);

        find_ = breakpoint->add_subcommand("find", "Find causal breakpoints on a branch");
        find_->add_option("branch", find_branch_, "Branch name")->required();
        find_->add_option("--store", store_path_, "Repository path")->default_val(".pvstore");

        rank_ = breakpoint->add_subcommand("rank", "Rank causal breakpoints by measured intervention cost");
        rank_->add_option("branch", rank_branch_, "Branch name")->required();
        rank_->add_option("--by", rank_by_, "Ranking basis")->default_val("intervention-cost");
        rank_->add_option("--store", store_path_, "Repository path")->default_val(".pvstore");

        explain_ = breakpoint->add_subcommand("explain", "Explain a causal breakpoint evidence chain");
        explain_->add_option("branch", explain_branch_, "Branch name")->required();
        explain_->add_option("breakpoint-id", explain_id_, "Breakpoint id")->required();
        explain_->add_option("--store", store_path_, "Repository path")->default_val(".pvstore");

        measure_ = breakpoint->add_subcommand("measure", "Measure a breakpoint by counterfactual intervention cost");
        measure_->add_option("branch", measure_branch_, "Branch name")->required();
        measure_->add_option("breakpoint-id", measure_id_, "Breakpoint id")->required();
        measure_->add_option("--store", store_path_, "Repository path")->default_val(".pvstore");

        repair_ = breakpoint->add_subcommand("repair", "Generate a minimal repair branch script");
        repair_->add_option("branch", repair_branch_, "Branch name")->required();
        repair_->add_option("breakpoint-id", repair_id_, "Breakpoint id")->required();
        repair_->add_option("--out", repair_out_, "Output .pv repair script");
        repair_->add_option("--store", store_path_, "Repository path")->default_val(".pvstore");

        repair_set_ = breakpoint->add_subcommand("repair-set", "Generate finite counterfactual repair candidates");
        repair_set_->add_option("branch", repair_set_branch_, "Branch name")->required();
        repair_set_->add_option("breakpoint-id", repair_set_id_, "Breakpoint id")->required();
        repair_set_->add_option("--store", store_path_, "Repository path")->default_val(".pvstore");

        fork_ = breakpoint->add_subcommand("fork", "Fork a branch, apply a repair, and verify replay");
        fork_->add_option("branch", fork_branch_, "Branch name")->required();
        fork_->add_option("breakpoint-id", fork_id_, "Breakpoint id")->required();
        fork_->add_option("--new-branch", fork_new_branch_, "New repaired branch")->required();
        fork_->add_option("--store", store_path_, "Repository path")->default_val(".pvstore");
    }

    int run() override {
        if (find_->parsed()) {
            return run_checked([&] {
                const auto repository = Repository::open(store_path_);
                const ProjectionStore store{repository};
                const auto breakpoints = BreakpointFinder{}.find(store, find_branch_);
                std::cout << render_breakpoints_text(find_branch_, breakpoints);
                return EXIT_SUCCESS;
            });
        }
        if (rank_->parsed()) {
            return run_checked([&] {
                if (rank_by_ != "intervention-cost") {
                    throw std::invalid_argument("breakpoint rank currently supports --by intervention-cost");
                }
                const auto repository = Repository::open(store_path_);
                const ProjectionStore store{repository};
                const auto measurements = BreakpointMeasure{}.rank(repository, store, rank_branch_);
                std::cout << render_breakpoint_rank_text(rank_branch_, measurements);
                return EXIT_SUCCESS;
            });
        }
        if (explain_->parsed()) {
            return run_checked([&] {
                const auto repository = Repository::open(store_path_);
                const ProjectionStore store{repository};
                const auto breakpoint = require_breakpoint(store, explain_branch_, explain_id_);
                const auto chain = EvidenceChainBuilder{}.build(store, explain_branch_, breakpoint);
                std::cout << render_evidence_chain_text(chain);
                return EXIT_SUCCESS;
            });
        }
        if (measure_->parsed()) {
            return run_checked([&] {
                const auto repository = Repository::open(store_path_);
                const ProjectionStore store{repository};
                const auto breakpoint = require_breakpoint(store, measure_branch_, measure_id_);
                const auto measurement = BreakpointMeasure{}.measure(repository, store, measure_branch_, breakpoint);
                std::cout << render_breakpoint_measure_text(measurement);
                return EXIT_SUCCESS;
            });
        }
        if (repair_->parsed()) {
            return run_checked([&] {
                const auto repository = Repository::open(store_path_);
                const ProjectionStore store{repository};
                const auto breakpoint = require_breakpoint(store, repair_branch_, repair_id_);
                const auto candidate = RepairCandidateBuilder{}.build(store, repair_branch_, breakpoint);
                if (repair_out_.empty()) {
                    std::cout << candidate.script;
                } else {
                    std::ofstream output(repair_out_, std::ios::trunc);
                    if (!output) {
                        throw std::runtime_error(fmt::format("cannot write '{}'", repair_out_));
                    }
                    output << candidate.script;
                    std::cout << fmt::format(
                        "Wrote {} repair candidate for {} to {}\n",
                        to_string(candidate.action),
                        candidate.breakpoint_id,
                        repair_out_);
                }
                return EXIT_SUCCESS;
            });
        }
        if (repair_set_->parsed()) {
            return run_checked([&] {
                const auto repository = Repository::open(store_path_);
                const ProjectionStore store{repository};
                const auto breakpoint = require_breakpoint(store, repair_set_branch_, repair_set_id_);
                const auto measurement = BreakpointMeasure{}.measure(repository, store, repair_set_branch_, breakpoint);
                std::cout << render_repair_set_text(measurement);
                return EXIT_SUCCESS;
            });
        }
        if (fork_->parsed()) {
            return run_checked([&] {
                auto repository = Repository::open(store_path_);
                {
                    const ProjectionStore store{repository};
                    const auto breakpoint = require_breakpoint(store, fork_branch_, fork_id_);
                    const auto candidate = RepairCandidateBuilder{}.build(store, fork_branch_, breakpoint);
                    const auto forked = repository.fork(fork_branch_, fork_new_branch_);

                    std::istringstream input{candidate.script};
                    std::ostringstream script_output;
                    cli::ScriptEngine engine{repository, fork_new_branch_};
                    if (!engine.run_stream(input, script_output, false)) {
                        throw std::runtime_error("repair script failed:\n" + script_output.str());
                    }

                    const auto report = IntegrityChecker{}.check_repository(repository);
                    std::cout << fmt::format(
                        "Forked {} from {} at {}\n",
                        fork_new_branch_,
                        fork_branch_,
                        short_hash(forked.base_commit));
                    std::cout << fmt::format("Applied repair: {}\n", to_string(candidate.action));
                    std::cout << fmt::format("Verified replay: {}\n", report.clean() ? "clean" : "errors");
                    for (const auto& error : report.errors) {
                        std::cout << fmt::format("error: {}\n", error.message);
                    }
                    return report.clean() ? EXIT_SUCCESS : EXIT_FAILURE;
                }
            });
        }
        return EXIT_SUCCESS;
    }

private:
    CLI::App* find_{nullptr};
    CLI::App* rank_{nullptr};
    CLI::App* explain_{nullptr};
    CLI::App* measure_{nullptr};
    CLI::App* repair_{nullptr};
    CLI::App* repair_set_{nullptr};
    CLI::App* fork_{nullptr};

    std::string store_path_{".pvstore"};
    std::string find_branch_;
    std::string rank_branch_;
    std::string rank_by_{"intervention-cost"};
    std::string explain_branch_;
    std::string explain_id_;
    std::string measure_branch_;
    std::string measure_id_;
    std::string repair_branch_;
    std::string repair_id_;
    std::string repair_out_;
    std::string repair_set_branch_;
    std::string repair_set_id_;
    std::string fork_branch_;
    std::string fork_id_;
    std::string fork_new_branch_;
};

}  // namespace

std::unique_ptr<cli_app::Command> make_breakpoint_command() {
    return std::make_unique<BreakpointCommand>();
}

}  // namespace pv::app
