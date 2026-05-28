// SPDX-License-Identifier: Apache-2.0
#include "commands.hpp"

#include <cstdlib>
#include <memory>
#include <string>

#include "command_utils.hpp"
#include "pv/core/world.hpp"
#include "pv/law/verifier.hpp"
#include "pv/runtime/world_store.hpp"
#include "pv/runtime/replayer.hpp"
#include "pv/trace/replayer.hpp"

namespace pv::app {
namespace {

class TraceCommand final : public cli_app::Command {
public:
    void register_with(CLI::App& app) override {
        auto* trace = app.add_subcommand("trace", "Replay and verify trace history");
        trace->require_subcommand(1);
        set_root(trace);

        replay_ = trace->add_subcommand("replay", "Replay a JSONL trace");
        replay_->add_option("trace", replay_trace_path_, "Path to a JSONL trace")->required();

        verify_ = trace->add_subcommand("verify", "Replay a JSONL trace and verify its final hash");
        verify_->add_option("trace", verify_trace_path_, "Path to a JSONL trace")->required();
        verify_->add_option("--expect-hash", expected_hash_, "Expected final hash")->required();
        expect_commits_option_ = verify_->add_option("--expect-commits", expected_commits_, "Expected replayed runtime commit count");
        verify_->add_option("--expect-branch", expected_branch_, "Expected runtime branch name")->default_val("main");
    }

    int run() override {
        if (replay_->parsed()) {
            return run_checked([&] {
                const auto jsonl = read_text_file(replay_trace_path_);
                const Verifier verifier;
                const auto result = TraceReplayer{}.replay_jsonl(jsonl, verifier);
                print_replay_report(result, result.errors.empty() ? "deterministic" : "errors");
                return result.errors.empty() ? EXIT_SUCCESS : EXIT_FAILURE;
            });
        }
        if (verify_->parsed()) {
            return run_checked([&] {
                const auto jsonl = read_text_file(verify_trace_path_);
                const Verifier verifier;
                WorldStore store;
                const auto world_name = first_world_name(jsonl, "world");
                const auto branch = store.create_branch(expected_branch_, World{world_name});
                const auto result = RuntimeReplayer{}.replay_into(store, branch, jsonl, verifier);
                const auto expected = parse_expected_hash(expected_hash_);
                const auto hash_ok = matches_expected_hash(result.final_hash, expected);
                const auto commits_ok = expect_commits_option_->count() == 0 || result.commits_replayed == expected_commits_;
                const auto branch_ok = result.branch_name == expected_branch_;
                const auto ok = result.errors.empty() && hash_ok && commits_ok && branch_ok;
                print_runtime_replay_report(result, ok ? "verified" : "hash mismatch");
                return ok ? EXIT_SUCCESS : EXIT_FAILURE;
            });
        }
        return EXIT_SUCCESS;
    }

private:
    CLI::App* replay_{nullptr};
    CLI::App* verify_{nullptr};
    CLI::Option* expect_commits_option_{nullptr};
    std::string replay_trace_path_;
    std::string verify_trace_path_;
    std::string expected_hash_;
    std::size_t expected_commits_{0};
    std::string expected_branch_{"main"};
};

}  // namespace

std::unique_ptr<cli_app::Command> make_trace_command() {
    return std::make_unique<TraceCommand>();
}

}  // namespace pv::app
