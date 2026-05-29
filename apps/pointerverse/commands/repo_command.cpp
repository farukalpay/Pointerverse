// SPDX-License-Identifier: Apache-2.0
#include "commands.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "command_utils.hpp"
#include "pv/cli/script.hpp"
#include "pv/core/world.hpp"
#include "pv/law/verifier.hpp"
#include "pv/query/explanation.hpp"
#include "pv/storage/repository.hpp"

namespace pv::app {
namespace {

class RepoCommand final : public cli_app::Command {
public:
    void register_with(CLI::App& app) override {
        auto* repo = app.add_subcommand("repo", "Persistent graph-world repository commands");
        repo->require_subcommand(1);
        repo->add_option("--store", repo_path_, "Repository path")->default_val(".pvstore");
        set_root(repo);

        init_ = repo->add_subcommand("init", "Initialize a Pointerverse repository");
        init_->add_option("path", init_path_, "Repository path")->default_val(".pvstore");

        status_ = repo->add_subcommand("status", "Show repository status");

        commit_ = repo->add_subcommand("commit", "Replay and commit a JSONL trace");
        commit_->add_option("trace", trace_path_, "Path to a JSONL trace")->required();
        commit_->add_option("--branch", branch_, "Branch to commit into; defaults to current branch");

        run_ = repo->add_subcommand("run", "Run a Pointerverse script against a repository branch");
        run_->add_option("script", run_script_, "Path to a .pv script")->required();
        run_->add_option("--branch", run_branch_, "Branch to run against; defaults to current branch");

        repl_ = repo->add_subcommand("repl", "Start a repository-backed Pointerverse world terminal");
        repl_->add_option("--branch", repl_branch_, "Branch to run against; defaults to current branch");

        query_ = repo->add_subcommand("query", "Query branch graph and history");
        query_->add_option("branch", query_branch_, "Branch name")->required();
        query_->add_option("query", query_terms_, "Query terms")->required()->expected(-1);

        query_file_ = repo->add_subcommand("query-file", "Run a saved query file (one query per line)");
        query_file_->add_option("branch", query_file_branch_, "Branch name")->required();
        query_file_->add_option("file", query_file_path_, "Path to a saved query file")->required();

        explain_ = repo->add_subcommand("explain", "Explain a branch object or commit");
        explain_->add_option("branch", explain_branch_, "Branch name")->required();
        explain_->add_option("kind", explain_kind_, "object | commit")->required();
        explain_->add_option("target", explain_target_, "Object name or commit prefix")->required();

        why_ = repo->add_subcommand("why", "Explain why a relation exists");
        why_->add_option("branch", why_branch_, "Branch name")->required();
        why_->add_option("from", why_from_, "Source object")->required();
        why_->add_option("relation", why_relation_, "Relation name")->required();
        why_->add_option("to", why_to_, "Target object")->required();

        verify_ = repo->add_subcommand("verify", "Verify repository integrity");
        fsck_ = repo->add_subcommand("fsck", "Check repository integrity");

        checkout_ = repo->add_subcommand("checkout", "Set current branch");
        checkout_->add_option("branch", checkout_branch_, "Branch name")->required();

        history_ = repo->add_subcommand("history", "Show branch commit history");
        history_->add_option("branch", history_branch_, "Branch name; defaults to current branch");

        branch_cmd_ = repo->add_subcommand("branch", "Branch commands");
        branch_cmd_->require_subcommand(1);
        branch_list_ = branch_cmd_->add_subcommand("list", "List branches");
        branch_fork_ = branch_cmd_->add_subcommand("fork", "Fork a branch");
        branch_fork_->add_option("source", source_branch_, "Source branch")->required();
        branch_fork_->add_option("name", new_branch_, "New branch")->required();
        branch_compare_ = branch_cmd_->add_subcommand("compare", "Compare two branches");
        branch_compare_->add_option("left", left_branch_, "Left branch")->required();
        branch_compare_->add_option("right", right_branch_, "Right branch")->required();
    }

    int run() override {
        if (init_->parsed()) {
            return run_checked([&] {
                auto repository = Repository::init(init_path_);
                std::cout << fmt::format("Initialized Pointerverse repository at {}\n", repository.root().string());
                return EXIT_SUCCESS;
            });
        }
        if (status_->parsed()) {
            return run_checked([&] {
                const auto repository = Repository::open(repo_path_);
                const auto status = repository.status();
                std::cout << "Repository status\n";
                std::cout << "-----------------\n";
                std::cout << fmt::format("store:           {}\n", status.root.string());
                std::cout << fmt::format("current branch:  {}\n", status.current_branch);
                std::cout << fmt::format("branches:        {}\n", status.branches);
                return EXIT_SUCCESS;
            });
        }
        if (commit_->parsed()) {
            return run_checked([&] {
                auto repository = Repository::open(repo_path_);
                const auto jsonl = read_text_file(trace_path_);
                const Verifier verifier;
                const auto branch = branch_.empty() ? repository.current_branch() : branch_;
                const auto result = repository.replay_trace(branch, jsonl, verifier);
                print_runtime_replay_report(result, result.errors.empty() ? "committed" : "errors");
                return result.errors.empty() ? EXIT_SUCCESS : EXIT_FAILURE;
            });
        }
        if (run_->parsed()) {
            return run_checked([&] {
                auto repository = Repository::open(repo_path_);
                const auto branch = run_branch_.empty() ? repository.current_branch() : run_branch_;
                if (!repository.has_branch(branch)) {
                    (void)repository.create_branch(branch, World{first_script_world_name(run_script_, "world")});
                }
                cli::ScriptEngine engine{repository, branch};
                return engine.run_file(run_script_, std::cout) ? EXIT_SUCCESS : EXIT_FAILURE;
            });
        }
        if (repl_->parsed()) {
            return run_checked([&] {
                auto repository = Repository::open(repo_path_);
                const auto branch = repl_branch_.empty() ? repository.current_branch() : repl_branch_;
                if (!repository.has_branch(branch)) {
                    (void)repository.create_branch(branch, World{"world"});
                }
                cli::ScriptEngine engine{repository, branch};
                std::cout << "Pointerverse repository terminal\n";
                std::cout << "Type help for commands, exit to quit.\n";
                return engine.run_stream(std::cin, std::cout, true) ? EXIT_SUCCESS : EXIT_FAILURE;
            });
        }
        if (query_->parsed()) {
            return run_checked([&] {
                const auto repository = Repository::open(repo_path_);
                const auto result = run_query(repository, query_branch_, query_terms_);
                print_query_result(repository.world(query_branch_).snapshot(), result);
                return EXIT_SUCCESS;
            });
        }
        if (query_file_->parsed()) {
            return run_checked([&] {
                const auto repository = Repository::open(repo_path_);
                const auto snapshot = repository.world(query_file_branch_).snapshot();
                std::ifstream input(query_file_path_);
                if (!input) {
                    throw std::runtime_error(fmt::format("cannot open query file '{}'", query_file_path_));
                }
                std::size_t executed = 0;
                std::string line;
                while (std::getline(input, line)) {
                    const auto marker = line.find('#');
                    std::istringstream tokens(marker == std::string::npos ? line : line.substr(0, marker));
                    std::vector<std::string> terms;
                    std::string token;
                    while (tokens >> token) {
                        terms.push_back(token);
                    }
                    if (terms.empty()) {
                        continue;
                    }
                    std::string joined;
                    for (const auto& term : terms) {
                        joined += joined.empty() ? term : " " + term;
                    }
                    std::cout << fmt::format("query: {}\n", joined);
                    print_query_result(snapshot, run_query(repository, query_file_branch_, terms));
                    std::cout << "\n";
                    executed += 1;
                }
                std::cout << fmt::format("ran {} quer{} from {}\n", executed, executed == 1 ? "y" : "ies", query_file_path_);
                return EXIT_SUCCESS;
            });
        }
        if (explain_->parsed()) {
            return run_checked([&] {
                const auto repository = Repository::open(repo_path_);
                const ExplanationEngine explain;
                if (explain_kind_ == "object") {
                    std::cout << explain.explain_object(repository, explain_branch_, explain_target_);
                    return EXIT_SUCCESS;
                }
                if (explain_kind_ == "commit") {
                    std::cout << explain.explain_commit(repository, explain_branch_, explain_target_);
                    return EXIT_SUCCESS;
                }
                throw std::invalid_argument("usage: repo explain BRANCH object|commit TARGET");
            });
        }
        if (why_->parsed()) {
            return run_checked([&] {
                const auto repository = Repository::open(repo_path_);
                std::cout << ExplanationEngine{}.why_relation(
                    repository,
                    why_branch_,
                    why_from_,
                    why_relation_,
                    why_to_);
                return EXIT_SUCCESS;
            });
        }
        if (verify_->parsed() || fsck_->parsed()) {
            return run_checked([&] {
                const auto repository = Repository::open(repo_path_);
                const auto report = IntegrityChecker{}.check_repository(repository);
                print_integrity_report(report);
                return report.clean() ? EXIT_SUCCESS : EXIT_FAILURE;
            });
        }
        if (branch_list_->parsed()) {
            return run_checked([&] {
                const auto repository = Repository::open(repo_path_);
                for (const auto& ref : repository.list_branches()) {
                    std::cout << fmt::format("{} epoch {} commit {} snapshot {}\n",
                        ref.name,
                        ref.epoch.value,
                        short_hash(ref.head),
                        to_hex(ref.snapshot).substr(0, 12));
                }
                return EXIT_SUCCESS;
            });
        }
        if (branch_fork_->parsed()) {
            return run_checked([&] {
                auto repository = Repository::open(repo_path_);
                const auto result = repository.fork(source_branch_, new_branch_);
                std::cout << fmt::format("Forked {} from {} at {}\n", new_branch_, source_branch_, short_hash(result.base_commit));
                return EXIT_SUCCESS;
            });
        }
        if (branch_compare_->parsed()) {
            return run_checked([&] {
                const auto repository = Repository::open(repo_path_);
                const auto analysis = repository.analyze_merge(left_branch_, right_branch_);
                std::cout << fmt::format(
                    "common ancestor: {}\n",
                    analysis.common_ancestor.has_value() ? short_hash(*analysis.common_ancestor) : "none");
                std::cout << fmt::format("status: {}\n", to_string(analysis.status));
                if (analysis.left_divergence.commit.has_value() || analysis.right_divergence.commit.has_value()) {
                    const auto print_divergence = [](std::string_view side, std::string_view branch, const DivergencePoint& point) {
                        if (point.commit.has_value()) {
                            std::cout << fmt::format(
                                "  {} ({}): {} {}\n",
                                side,
                                branch,
                                short_hash(*point.commit),
                                point.label.empty() ? "(unlabeled)" : point.label);
                        } else {
                            std::cout << fmt::format("  {} ({}): none (at fork point)\n", side, branch);
                        }
                    };
                    std::cout << "\nfirst divergent commit:\n";
                    print_divergence("left", left_branch_, analysis.left_divergence);
                    print_divergence("right", right_branch_, analysis.right_divergence);
                }
                if (!analysis.object_conflicts.empty()) {
                    std::cout << "\nobject conflicts:\n";
                    for (const auto& conflict : analysis.object_conflicts) {
                        std::cout << fmt::format("  {}: {}\n", conflict.name, conflict.reason);
                    }
                }
                if (!analysis.law_drifts.empty()) {
                    std::cout << "\nlaw drift:\n";
                    for (const auto& drift : analysis.law_drifts) {
                        std::cout << fmt::format("  left:  {}\n", to_hex(drift.left_law_hash));
                        std::cout << fmt::format("  right: {}\n", to_hex(drift.right_law_hash));
                    }
                }
                return analysis.status == MergeStatus::Clean ? EXIT_SUCCESS : EXIT_FAILURE;
            });
        }
        if (checkout_->parsed()) {
            return run_checked([&] {
                auto repository = Repository::open(repo_path_);
                repository.checkout(checkout_branch_);
                std::cout << fmt::format("Checked out {}\n", checkout_branch_);
                return EXIT_SUCCESS;
            });
        }
        if (history_->parsed()) {
            return run_checked([&] {
                const auto repository = Repository::open(repo_path_);
                const auto branch = history_branch_.empty() ? repository.current_branch() : history_branch_;
                std::cout << branch << "\n";
                std::cout << std::string(branch.size(), '-') << "\n";
                for (const auto& record : repository.history(branch)) {
                    std::cout << fmt::format(
                        "{}  {:<20} epoch {}   snapshot {}   delta {}\n",
                        short_hash(record.id),
                        record.label.empty() ? "(unlabeled)" : record.label,
                        record.after_epoch.value,
                        to_hex(record.after_hash).substr(0, 12),
                        to_hex(record.delta_hash).substr(0, 12));
                }
                return EXIT_SUCCESS;
            });
        }
        return EXIT_SUCCESS;
    }

private:
    CLI::App* init_{nullptr};
    CLI::App* status_{nullptr};
    CLI::App* commit_{nullptr};
    CLI::App* run_{nullptr};
    CLI::App* repl_{nullptr};
    CLI::App* query_{nullptr};
    CLI::App* query_file_{nullptr};
    CLI::App* explain_{nullptr};
    CLI::App* why_{nullptr};
    CLI::App* verify_{nullptr};
    CLI::App* fsck_{nullptr};
    CLI::App* checkout_{nullptr};
    CLI::App* history_{nullptr};
    CLI::App* branch_cmd_{nullptr};
    CLI::App* branch_list_{nullptr};
    CLI::App* branch_fork_{nullptr};
    CLI::App* branch_compare_{nullptr};

    std::string repo_path_{".pvstore"};
    std::string init_path_{".pvstore"};
    std::string trace_path_;
    std::string branch_;
    std::string run_script_;
    std::string run_branch_;
    std::string repl_branch_;
    std::string query_branch_;
    std::vector<std::string> query_terms_;
    std::string query_file_branch_;
    std::string query_file_path_;
    std::string explain_branch_;
    std::string explain_kind_;
    std::string explain_target_;
    std::string why_branch_;
    std::string why_from_;
    std::string why_relation_;
    std::string why_to_;
    std::string checkout_branch_;
    std::string history_branch_;
    std::string source_branch_;
    std::string new_branch_;
    std::string left_branch_;
    std::string right_branch_;
};

}  // namespace

std::unique_ptr<cli_app::Command> make_repo_command() {
    return std::make_unique<RepoCommand>();
}

}  // namespace pv::app
