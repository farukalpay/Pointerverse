#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

#include <CLI/CLI.hpp>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "pv/cli/script.hpp"
#include "pv/core/world.hpp"
#include "pv/hash/canonical.hpp"
#include "pv/runtime/replayer.hpp"
#include "pv/runtime/world_store.hpp"
#include "pv/storage/integrity.hpp"
#include "pv/storage/repository.hpp"
#include "pv/trace/replayer.hpp"

namespace {

std::string read_text_file(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error(fmt::format("cannot open trace '{}'", path));
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

struct ExpectedHash {
    std::optional<pv::Hash256> canonical;
    std::optional<std::uint64_t> legacy;
};

ExpectedHash parse_expected_hash(const std::string& value) {
    if (auto canonical = pv::parse_hash256(value); canonical.has_value()) {
        return ExpectedHash{canonical, std::nullopt};
    }

    std::size_t consumed = 0;
    const auto base = value.rfind("0x", 0) == 0 || value.rfind("0X", 0) == 0 ? 16 : 10;
    const auto parsed = std::stoull(value, &consumed, base);
    if (consumed != value.size()) {
        throw std::invalid_argument(fmt::format("invalid hash '{}'", value));
    }
    return ExpectedHash{std::nullopt, parsed};
}

bool matches_expected_hash(pv::Hash256 actual, const ExpectedHash& expected) {
    if (expected.canonical.has_value()) {
        return actual == *expected.canonical;
    }
    return expected.legacy.has_value() && pv::truncated_u64(actual) == *expected.legacy;
}

std::string first_world_name(std::string_view jsonl, std::string fallback) {
    std::istringstream input{std::string{jsonl}};
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        try {
            const auto json = nlohmann::json::parse(line);
            const auto fields = json.value("fields", nlohmann::json::object());
            if (!fields.is_object()) {
                continue;
            }
            const auto iter = fields.find("world");
            if (iter != fields.end() && iter->is_string() && !iter->get<std::string>().empty()) {
                return iter->get<std::string>();
            }
        } catch (const std::exception&) {
            return fallback;
        }
    }
    return fallback;
}

void print_replay_report(const pv::ReplayResult& result, std::string_view status) {
    std::cout << "Replay report\n";
    std::cout << "-------------\n";
    std::cout << fmt::format("events read:      {}\n", result.events_read);
    std::cout << fmt::format("events replayed:  {}\n", result.events_replayed);
    std::cout << fmt::format("metadata events:  {}\n", result.metadata_events);
    std::cout << fmt::format("final epoch:      {}\n", result.world.epoch().value);
    std::cout << fmt::format("final hash:       0x{:016x}\n", result.final_hash);
    std::cout << fmt::format("status:           {}\n", status);
    for (const auto& error : result.errors) {
        std::cout << fmt::format("error line {} {}: {}\n", error.line, error.event, error.message);
    }
}

void print_runtime_replay_report(const pv::RuntimeReplayResult& result, std::string_view status) {
    std::cout << "Replay report\n";
    std::cout << "-------------\n";
    std::cout << fmt::format("events read:      {}\n", result.events_read);
    std::cout << fmt::format("events replayed:  {}\n", result.events_replayed);
    std::cout << fmt::format("metadata events:  {}\n", result.metadata_events);
    std::cout << fmt::format("commits replayed: {}\n", result.commits_replayed);
    std::cout << fmt::format("branch:           {}\n", result.branch_name);
    std::cout << fmt::format("final hash:       0x{:016x}\n", pv::truncated_u64(result.final_hash));
    std::cout << fmt::format("final hash256:    {}\n", pv::to_hex(result.final_hash));
    std::cout << fmt::format("status:           {}\n", status);
    for (const auto& error : result.errors) {
        std::cout << fmt::format("error line {} {}: {}\n", error.line, error.event, error.message);
    }
}

void print_integrity_report(const pv::IntegrityReport& report) {
    std::cout << "Repository integrity\n";
    std::cout << "--------------------\n";
    std::cout << fmt::format("objects checked:    {}\n", report.objects_checked);
    std::cout << fmt::format("commits checked:    {}\n", report.commits_checked);
    std::cout << fmt::format("snapshots checked:  {}\n", report.snapshots_checked);
    std::cout << fmt::format("branch refs:        {}\n", report.branch_refs_checked);
    std::cout << fmt::format("status:             {}\n", report.clean() ? "clean" : "errors");
    for (const auto& error : report.errors) {
        std::cout << fmt::format("error: {}\n", error.message);
    }
    for (const auto& warning : report.warnings) {
        std::cout << fmt::format("warning: {}\n", warning.message);
    }
}

std::string short_hash(pv::CommitId id) {
    return pv::to_hex(id.value).substr(0, 12);
}

}  // namespace

int main(int argc, char** argv) {
    CLI::App app{"Pointerverse categorical reality lab"};
    app.require_subcommand(1);

    std::string script_path;
    std::string replay_trace_path;
    std::string verify_trace_path;
    std::string expected_hash;
    std::size_t expected_commits = 0;
    std::string expected_branch = "main";
    std::string repo_path = ".pvstore";
    std::string repo_init_path = ".pvstore";
    std::string repo_trace_path;
    std::string repo_branch = "";
    std::string repo_source_branch;
    std::string repo_new_branch;
    std::string repo_left_branch;
    std::string repo_right_branch;
    std::string repo_checkout_branch;
    std::string repo_history_branch;

    auto* lab = app.add_subcommand("lab", "Run a Pointerverse script");
    lab->add_option("script", script_path, "Path to a .pv script")->required();

    auto* repl = app.add_subcommand("repl", "Start the Pointerverse REPL");

    auto* trace = app.add_subcommand("trace", "Replay and verify trace history");
    trace->require_subcommand(1);
    auto* trace_replay = trace->add_subcommand("replay", "Replay a JSONL trace");
    trace_replay->add_option("trace", replay_trace_path, "Path to a JSONL trace")->required();
    auto* trace_verify = trace->add_subcommand("verify", "Replay a JSONL trace and verify its final hash");
    trace_verify->add_option("trace", verify_trace_path, "Path to a JSONL trace")->required();
    trace_verify->add_option("--expect-hash", expected_hash, "Expected final hash")->required();
    auto* expect_commits_option = trace_verify->add_option("--expect-commits", expected_commits, "Expected replayed runtime commit count");
    trace_verify->add_option("--expect-branch", expected_branch, "Expected runtime branch name")->default_val("main");

    auto* repo = app.add_subcommand("repo", "Persistent reality repository commands");
    repo->require_subcommand(1);
    repo->add_option("--store", repo_path, "Repository path")->default_val(".pvstore");

    auto* repo_init = repo->add_subcommand("init", "Initialize a Pointerverse repository");
    repo_init->add_option("path", repo_init_path, "Repository path")->default_val(".pvstore");

    auto* repo_status = repo->add_subcommand("status", "Show repository status");

    auto* repo_commit = repo->add_subcommand("commit", "Replay and commit a JSONL trace");
    repo_commit->add_option("trace", repo_trace_path, "Path to a JSONL trace")->required();
    repo_commit->add_option("--branch", repo_branch, "Branch to commit into; defaults to current branch");

    auto* repo_verify = repo->add_subcommand("verify", "Verify repository integrity");
    auto* repo_fsck = repo->add_subcommand("fsck", "Check repository integrity");

    auto* repo_checkout = repo->add_subcommand("checkout", "Set current branch");
    repo_checkout->add_option("branch", repo_checkout_branch, "Branch name")->required();

    auto* repo_history = repo->add_subcommand("history", "Show branch commit history");
    repo_history->add_option("branch", repo_history_branch, "Branch name; defaults to current branch");

    auto* repo_branch_cmd = repo->add_subcommand("branch", "Branch commands");
    repo_branch_cmd->require_subcommand(1);
    auto* repo_branch_list = repo_branch_cmd->add_subcommand("list", "List branches");
    auto* repo_branch_fork = repo_branch_cmd->add_subcommand("fork", "Fork a branch");
    repo_branch_fork->add_option("source", repo_source_branch, "Source branch")->required();
    repo_branch_fork->add_option("name", repo_new_branch, "New branch")->required();
    auto* repo_branch_compare = repo_branch_cmd->add_subcommand("compare", "Compare two branches");
    repo_branch_compare->add_option("left", repo_left_branch, "Left branch")->required();
    repo_branch_compare->add_option("right", repo_right_branch, "Right branch")->required();

    CLI11_PARSE(app, argc, argv);

    if (trace_replay->parsed()) {
        try {
            const auto jsonl = read_text_file(replay_trace_path);
            const pv::Verifier verifier;
            const auto result = pv::TraceReplayer{}.replay_jsonl(jsonl, verifier);
            print_replay_report(result, result.errors.empty() ? "deterministic" : "errors");
            return result.errors.empty() ? EXIT_SUCCESS : EXIT_FAILURE;
        } catch (const std::exception& error) {
            std::cerr << fmt::format("error: {}\n", error.what());
            return EXIT_FAILURE;
        }
    }

    if (trace_verify->parsed()) {
        try {
            const auto jsonl = read_text_file(verify_trace_path);
            const pv::Verifier verifier;
            pv::WorldStore store;
            const auto world_name = first_world_name(jsonl, "world");
            const auto branch = store.create_branch(expected_branch, pv::World{world_name});
            const auto result = pv::RuntimeReplayer{}.replay_into(store, branch, jsonl, verifier);
            const auto expected = parse_expected_hash(expected_hash);
            const auto hash_ok = matches_expected_hash(result.final_hash, expected);
            const auto commits_ok = expect_commits_option->count() == 0 || result.commits_replayed == expected_commits;
            const auto branch_ok = result.branch_name == expected_branch;
            const auto ok = result.errors.empty() && hash_ok && commits_ok && branch_ok;
            print_runtime_replay_report(result, ok ? "verified" : "hash mismatch");
            return ok ? EXIT_SUCCESS : EXIT_FAILURE;
        } catch (const std::exception& error) {
            std::cerr << fmt::format("error: {}\n", error.what());
            return EXIT_FAILURE;
        }
    }

    if (repo_init->parsed()) {
        try {
            auto repository = pv::Repository::init(repo_init_path);
            std::cout << fmt::format("Initialized Pointerverse repository at {}\n", repository.root().string());
            return EXIT_SUCCESS;
        } catch (const std::exception& error) {
            std::cerr << fmt::format("error: {}\n", error.what());
            return EXIT_FAILURE;
        }
    }

    if (repo_status->parsed()) {
        try {
            const auto repository = pv::Repository::open(repo_path);
            const auto status = repository.status();
            std::cout << "Repository status\n";
            std::cout << "-----------------\n";
            std::cout << fmt::format("store:           {}\n", status.root.string());
            std::cout << fmt::format("current branch:  {}\n", status.current_branch);
            std::cout << fmt::format("branches:        {}\n", status.branches);
            return EXIT_SUCCESS;
        } catch (const std::exception& error) {
            std::cerr << fmt::format("error: {}\n", error.what());
            return EXIT_FAILURE;
        }
    }

    if (repo_commit->parsed()) {
        try {
            auto repository = pv::Repository::open(repo_path);
            const auto jsonl = read_text_file(repo_trace_path);
            const pv::Verifier verifier;
            const auto branch = repo_branch.empty() ? repository.current_branch() : repo_branch;
            const auto result = repository.replay_trace(branch, jsonl, verifier);
            print_runtime_replay_report(result, result.errors.empty() ? "committed" : "errors");
            return result.errors.empty() ? EXIT_SUCCESS : EXIT_FAILURE;
        } catch (const std::exception& error) {
            std::cerr << fmt::format("error: {}\n", error.what());
            return EXIT_FAILURE;
        }
    }

    if (repo_verify->parsed() || repo_fsck->parsed()) {
        try {
            const auto repository = pv::Repository::open(repo_path);
            const auto report = pv::IntegrityChecker{}.check_repository(repository);
            print_integrity_report(report);
            return report.clean() ? EXIT_SUCCESS : EXIT_FAILURE;
        } catch (const std::exception& error) {
            std::cerr << fmt::format("error: {}\n", error.what());
            return EXIT_FAILURE;
        }
    }

    if (repo_branch_list->parsed()) {
        try {
            const auto repository = pv::Repository::open(repo_path);
            for (const auto& ref : repository.list_branches()) {
                std::cout << fmt::format("{} epoch {} commit {} snapshot {}\n",
                    ref.name,
                    ref.epoch.value,
                    short_hash(ref.head),
                    pv::to_hex(ref.snapshot).substr(0, 12));
            }
            return EXIT_SUCCESS;
        } catch (const std::exception& error) {
            std::cerr << fmt::format("error: {}\n", error.what());
            return EXIT_FAILURE;
        }
    }

    if (repo_branch_fork->parsed()) {
        try {
            auto repository = pv::Repository::open(repo_path);
            const auto result = repository.fork(repo_source_branch, repo_new_branch);
            std::cout << fmt::format(
                "Forked {} from {} at {}\n",
                repo_new_branch,
                repo_source_branch,
                short_hash(result.base_commit));
            return EXIT_SUCCESS;
        } catch (const std::exception& error) {
            std::cerr << fmt::format("error: {}\n", error.what());
            return EXIT_FAILURE;
        }
    }

    if (repo_branch_compare->parsed()) {
        try {
            const auto repository = pv::Repository::open(repo_path);
            const auto analysis = repository.analyze_merge(repo_left_branch, repo_right_branch);
            std::cout << fmt::format(
                "common ancestor: {}\n",
                analysis.common_ancestor.has_value() ? short_hash(*analysis.common_ancestor) : "none");
            std::cout << fmt::format("status: {}\n", pv::to_string(analysis.status));
            if (!analysis.object_conflicts.empty()) {
                std::cout << "\nobject conflicts:\n";
                for (const auto& conflict : analysis.object_conflicts) {
                    std::cout << fmt::format("  {}: {}\n", conflict.name, conflict.reason);
                }
            }
            if (!analysis.law_drifts.empty()) {
                std::cout << "\nlaw drift:\n";
                for (const auto& drift : analysis.law_drifts) {
                    std::cout << fmt::format("  left:  {}\n", pv::to_hex(drift.left_law_hash));
                    std::cout << fmt::format("  right: {}\n", pv::to_hex(drift.right_law_hash));
                }
            }
            return analysis.status == pv::MergeStatus::Clean ? EXIT_SUCCESS : EXIT_FAILURE;
        } catch (const std::exception& error) {
            std::cerr << fmt::format("error: {}\n", error.what());
            return EXIT_FAILURE;
        }
    }

    if (repo_checkout->parsed()) {
        try {
            auto repository = pv::Repository::open(repo_path);
            repository.checkout(repo_checkout_branch);
            std::cout << fmt::format("Checked out {}\n", repo_checkout_branch);
            return EXIT_SUCCESS;
        } catch (const std::exception& error) {
            std::cerr << fmt::format("error: {}\n", error.what());
            return EXIT_FAILURE;
        }
    }

    if (repo_history->parsed()) {
        try {
            const auto repository = pv::Repository::open(repo_path);
            const auto branch = repo_history_branch.empty() ? repository.current_branch() : repo_history_branch;
            std::cout << branch << "\n";
            std::cout << std::string(branch.size(), '-') << "\n";
            for (const auto& record : repository.history(branch)) {
                std::cout << fmt::format(
                    "{}  {:<20} epoch {}   snapshot {}   delta {}\n",
                    short_hash(record.id),
                    record.label.empty() ? "(unlabeled)" : record.label,
                    record.after_epoch.value,
                    pv::to_hex(record.after_hash).substr(0, 12),
                    pv::to_hex(record.delta_hash).substr(0, 12));
            }
            return EXIT_SUCCESS;
        } catch (const std::exception& error) {
            std::cerr << fmt::format("error: {}\n", error.what());
            return EXIT_FAILURE;
        }
    }

    pv::World world;
    pv::cli::ScriptEngine engine{world};

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
