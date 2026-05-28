#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include <CLI/CLI.hpp>
#include <fmt/format.h>

#include "pv/cli/script.hpp"
#include "pv/core/world.hpp"
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

std::uint64_t parse_hash(const std::string& value) {
    std::size_t consumed = 0;
    const auto base = value.rfind("0x", 0) == 0 || value.rfind("0X", 0) == 0 ? 16 : 10;
    const auto parsed = std::stoull(value, &consumed, base);
    if (consumed != value.size()) {
        throw std::invalid_argument(fmt::format("invalid hash '{}'", value));
    }
    return parsed;
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

}  // namespace

int main(int argc, char** argv) {
    CLI::App app{"Pointerverse categorical reality lab"};
    app.require_subcommand(1);

    std::string script_path;
    std::string replay_trace_path;
    std::string verify_trace_path;
    std::string expected_hash;

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
            const auto result = pv::TraceReplayer{}.replay_jsonl(jsonl, verifier);
            const auto expected = parse_hash(expected_hash);
            const auto ok = result.errors.empty() && result.final_hash == expected;
            print_replay_report(result, ok ? "verified" : "hash mismatch");
            return ok ? EXIT_SUCCESS : EXIT_FAILURE;
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
