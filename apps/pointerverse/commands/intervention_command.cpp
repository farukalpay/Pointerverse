// SPDX-License-Identifier: Apache-2.0
#include "commands.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include <fmt/format.h>

#include "command_utils.hpp"
#include "pv/breakpoint/breakpoint_finder.hpp"
#include "pv/intervention/intervention_search.hpp"
#include "pv/projection/projection_store.hpp"
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

std::string short_hash(Hash256 hash) {
    return to_hex(hash).substr(0, 12);
}

bool id_matches(std::string_view id, std::string_view full_or_short) {
    return full_or_short.rfind(id, 0) == 0;
}

std::optional<InterventionProgram> find_program(
    const InterventionSearchResult& result,
    std::string_view id) {
    if (id_matches(id, to_hex(result.search_id)) && result.minimal_killing_program.has_value()) {
        return result.minimal_killing_program;
    }
    for (const auto& sample : result.samples) {
        if (id_matches(id, to_hex(sample.program.canonical_hash))
            || id_matches(id, intervention_program_id(sample.program))) {
            return sample.program;
        }
    }
    return std::nullopt;
}

struct ResolvedTrace {
    Breakpoint breakpoint;
    InterventionSearchResult search;
    InterventionProgram program;
    InterventionSearchOptions options;
};

ResolvedTrace resolve_trace(
    const Repository& repository,
    const ProjectionStore& store,
    std::string_view branch,
    std::string_view search_id) {
    const auto breakpoints = BreakpointFinder{}.find(store, branch);
    for (const auto& breakpoint : breakpoints) {
        const auto families = OperatorFamilyBuilder{}.build(store, branch, breakpoint);
        for (std::uint8_t depth = 0; depth <= 8; ++depth) {
            for (std::uint8_t composition = 1; composition <= 2; ++composition) {
                InterventionSearchOptions options;
                options.max_depth = depth;
                options.max_composition = composition;
                const auto candidate_id = intervention_search_id(repository, branch, breakpoint, families, options, {});
                if (id_matches(search_id, to_hex(candidate_id))) {
                    auto search = InterventionSearch{}.search(repository, store, branch, breakpoint, nullptr, {}, options);
                    auto program = search.minimal_killing_program.value_or(identity_intervention_program());
                    return ResolvedTrace{breakpoint, std::move(search), std::move(program), options};
                }
            }
        }
    }
    throw std::invalid_argument(fmt::format("cannot reconstruct intervention search '{}'", search_id));
}

class InterventionCommand final : public cli_app::Command {
public:
    void register_with(CLI::App& app) override {
        auto* intervention = app.add_subcommand("intervention", "Inspect replay-backed intervention algebra");
        intervention->require_subcommand(1);
        set_root(intervention);

        families_ = intervention->add_subcommand("families", "List operator families for a breakpoint");
        families_->add_option("branch", families_branch_, "Branch name")->required();
        families_->add_option("breakpoint-id", families_id_, "Breakpoint id")->required();
        families_->add_option("--store", store_path_, "Repository path")->default_val(".pvstore");

        refine_ = intervention->add_subcommand("refine", "Render dyadic operator refinement");
        refine_->add_option("branch", refine_branch_, "Branch name")->required();
        refine_->add_option("breakpoint-id", refine_id_, "Breakpoint id")->required();
        refine_->add_option("--depth", refine_depth_, "Dyadic refinement depth")->default_val(4);
        refine_->add_option("--store", store_path_, "Repository path")->default_val(".pvstore");

        search_ = intervention->add_subcommand("search", "Search replay-backed intervention programs");
        search_->add_option("branch", search_branch_, "Branch name")->required();
        search_->add_option("breakpoint-id", search_id_, "Breakpoint id")->required();
        search_->add_option("--max-depth", search_depth_, "Dyadic refinement depth")->default_val(2);
        search_->add_option("--max-composition", search_composition_, "Maximum composition size")->default_val(2);
        search_->add_option("--store", store_path_, "Repository path")->default_val(".pvstore");

        trace_ = intervention->add_subcommand("trace", "Recompute a deterministic intervention trace");
        trace_->add_option("branch", trace_branch_, "Branch name")->required();
        trace_->add_option("search-id", trace_id_, "Search id prefix")->required();
        trace_->add_option("--store", store_path_, "Repository path")->default_val(".pvstore");

        lattice_ = intervention->add_subcommand("lattice", "Render intervention lattice alternatives");
        lattice_->add_option("branch", lattice_branch_, "Branch name")->required();
        lattice_->add_option("breakpoint-id", lattice_id_, "Breakpoint id")->required();
        lattice_->add_option("--store", store_path_, "Repository path")->default_val(".pvstore");

        compose_ = intervention->add_subcommand("compose", "Compose exactly two intervention programs");
        compose_->add_option("branch", compose_branch_, "Branch name")->required();
        compose_->add_option("breakpoint-id", compose_breakpoint_id_, "Breakpoint id")->required();
        compose_->add_option("left-id", compose_left_id_, "Left intervention/search id")->required();
        compose_->add_option("right-id", compose_right_id_, "Right intervention/search id")->required();
        compose_->add_option("--store", store_path_, "Repository path")->default_val(".pvstore");
    }

    int run() override {
        if (families_->parsed()) {
            return run_checked([&] {
                const auto repository = Repository::open(store_path_);
                const ProjectionStore store{repository};
                const auto breakpoint = require_breakpoint(store, families_branch_, families_id_);
                const auto families = OperatorFamilyBuilder{}.build(store, families_branch_, breakpoint);
                std::cout << render_intervention_families_text(families);
                return EXIT_SUCCESS;
            });
        }
        if (refine_->parsed()) {
            return run_checked([&] {
                const auto repository = Repository::open(store_path_);
                const ProjectionStore store{repository};
                const auto breakpoint = require_breakpoint(store, refine_branch_, refine_id_);
                const auto families = OperatorFamilyBuilder{}.build(store, refine_branch_, breakpoint);
                std::cout << render_intervention_refinement_text(families, static_cast<std::uint8_t>(refine_depth_));
                return EXIT_SUCCESS;
            });
        }
        if (search_->parsed()) {
            return run_checked([&] {
                if (search_composition_ > 2) {
                    throw std::invalid_argument("intervention search v1 supports --max-composition up to 2");
                }
                const auto repository = Repository::open(store_path_);
                const ProjectionStore store{repository};
                const auto breakpoint = require_breakpoint(store, search_branch_, search_id_);
                InterventionSearchOptions options;
                options.max_depth = static_cast<std::uint8_t>(search_depth_);
                options.max_composition = static_cast<std::uint8_t>(search_composition_);
                const auto result = InterventionSearch{}.search(repository, store, search_branch_, breakpoint, nullptr, {}, options);
                std::cout << render_intervention_search_text(result);
                return EXIT_SUCCESS;
            });
        }
        if (trace_->parsed()) {
            return run_checked([&] {
                const auto repository = Repository::open(store_path_);
                const ProjectionStore store{repository};
                const auto resolved = resolve_trace(repository, store, trace_branch_, trace_id_);
                const auto trace = InterventionSearch{}.trace(
                    repository,
                    store,
                    trace_branch_,
                    resolved.breakpoint,
                    resolved.program,
                    nullptr,
                    {},
                    resolved.options);
                if (short_hash(trace.search_id) != short_hash(resolved.search.search_id)) {
                    throw std::runtime_error("reconstructed trace id mismatch");
                }
                std::cout << render_intervention_trace_text(trace);
                return EXIT_SUCCESS;
            });
        }
        if (lattice_->parsed()) {
            return run_checked([&] {
                const auto repository = Repository::open(store_path_);
                const ProjectionStore store{repository};
                const auto breakpoint = require_breakpoint(store, lattice_branch_, lattice_id_);
                const auto result = InterventionSearch{}.search(repository, store, lattice_branch_, breakpoint);
                std::cout << render_intervention_lattice_text(result);
                return EXIT_SUCCESS;
            });
        }
        if (compose_->parsed()) {
            return run_checked([&] {
                const auto repository = Repository::open(store_path_);
                const ProjectionStore store{repository};
                const auto breakpoint = require_breakpoint(store, compose_branch_, compose_breakpoint_id_);
                const auto result = InterventionSearch{}.search(repository, store, compose_branch_, breakpoint);
                const auto left = find_program(result, compose_left_id_);
                const auto right = find_program(result, compose_right_id_);
                if (!left.has_value() || !right.has_value()) {
                    throw std::invalid_argument("cannot resolve both intervention ids for composition");
                }
                const auto composed = InterventionSearch{}.compose_pair(
                    repository,
                    store,
                    compose_branch_,
                    breakpoint,
                    *left,
                    *right);
                std::cout << render_intervention_composition_text(composed);
                return EXIT_SUCCESS;
            });
        }
        return EXIT_SUCCESS;
    }

private:
    CLI::App* families_{nullptr};
    CLI::App* refine_{nullptr};
    CLI::App* search_{nullptr};
    CLI::App* trace_{nullptr};
    CLI::App* lattice_{nullptr};
    CLI::App* compose_{nullptr};

    std::string store_path_{".pvstore"};
    std::string families_branch_;
    std::string families_id_;
    std::string refine_branch_;
    std::string refine_id_;
    int refine_depth_{4};
    std::string search_branch_;
    std::string search_id_;
    int search_depth_{2};
    int search_composition_{2};
    std::string trace_branch_;
    std::string trace_id_;
    std::string lattice_branch_;
    std::string lattice_id_;
    std::string compose_branch_;
    std::string compose_breakpoint_id_;
    std::string compose_left_id_;
    std::string compose_right_id_;
};

}  // namespace

std::unique_ptr<cli_app::Command> make_intervention_command() {
    return std::make_unique<InterventionCommand>();
}

}  // namespace pv::app
