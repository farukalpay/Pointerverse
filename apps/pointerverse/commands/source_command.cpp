// SPDX-License-Identifier: Apache-2.0
#include "commands.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <fmt/format.h>

#include "command_utils.hpp"
#include "pv/normalize/canonical_event.hpp"
#include "pv/normalize/event_normalizer.hpp"
#include "pv/source/source_adapter.hpp"

namespace pv::app {
namespace {

void require_jsonl(std::string_view format) {
    if (format != "jsonl") {
        throw std::invalid_argument("format must be jsonl");
    }
}

SourceBatch read_source_batch(const std::string& path, std::string_view format) {
    require_jsonl(format);
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error(fmt::format("cannot open source '{}'", path));
    }
    return JsonlSourceAdapter{"jsonl"}.read(input);
}

void print_source_report(const SourceBatch& batch) {
    std::cout << "Source inspect\n";
    std::cout << "--------------\n";
    std::cout << fmt::format("events read: {}\n", batch.events.size());
    std::cout << fmt::format("errors:      {}\n", batch.errors.size());
    for (const auto& event : batch.events) {
        std::cout << fmt::format(
            "event {} {} {} -> {} : {}\n",
            event.source,
            event.id,
            event.actor.empty() ? "?" : event.actor,
            event.subject.empty() ? "?" : event.subject,
            event.relation.empty() ? "?" : event.relation);
    }
    for (const auto& error : batch.errors) {
        std::cout << fmt::format("error line {}: {}\n", error.line, error.message);
    }
}

class SourceCommand final : public cli_app::Command {
public:
    void register_with(CLI::App& app) override {
        auto* source = app.add_subcommand("source", "Inspect and normalize external source events");
        source->require_subcommand(1);
        set_root(source);

        inspect_ = source->add_subcommand("inspect", "Inspect a raw source event stream");
        inspect_->add_option("input", inspect_path_, "Input source log")->required();
        inspect_->add_option("--format", inspect_format_, "Source format")->default_val("jsonl");

        normalize_ = source->add_subcommand("normalize", "Normalize a raw source stream to canonical events");
        normalize_->add_option("input", normalize_path_, "Input source log")->required();
        normalize_->add_option("--format", normalize_format_, "Source format")->default_val("jsonl");
        normalize_->add_option("--out", out_path_, "Output canonical JSONL")->required();
    }

    int run() override {
        if (inspect_->parsed()) {
            return run_checked([&] {
                const auto batch = read_source_batch(inspect_path_, inspect_format_);
                print_source_report(batch);
                return batch.errors.empty() ? EXIT_SUCCESS : EXIT_FAILURE;
            });
        }
        if (normalize_->parsed()) {
            return run_checked([&] {
                const auto batch = read_source_batch(normalize_path_, normalize_format_);
                SourceEventNormalizer normalizer;
                std::ofstream output(out_path_);
                if (!output) {
                    throw std::runtime_error(fmt::format("cannot write '{}'", out_path_));
                }
                std::size_t normalized = 0;
                for (const auto& event : batch.events) {
                    output << to_jsonl(normalizer.normalize(event));
                    normalized += 1;
                }
                output.close();
                if (!output) {
                    throw std::runtime_error(fmt::format("failed writing '{}'", out_path_));
                }
                std::cout << "Source normalize\n";
                std::cout << "----------------\n";
                std::cout << fmt::format("events read: {}\n", batch.events.size());
                std::cout << fmt::format("normalized:  {}\n", normalized);
                std::cout << fmt::format("errors:      {}\n", batch.errors.size());
                for (const auto& error : batch.errors) {
                    std::cout << fmt::format("error line {}: {}\n", error.line, error.message);
                }
                return batch.errors.empty() ? EXIT_SUCCESS : EXIT_FAILURE;
            });
        }
        return EXIT_SUCCESS;
    }

private:
    CLI::App* inspect_{nullptr};
    CLI::App* normalize_{nullptr};
    std::string inspect_path_;
    std::string inspect_format_{"jsonl"};
    std::string normalize_path_;
    std::string normalize_format_{"jsonl"};
    std::string out_path_;
};

}  // namespace

std::unique_ptr<cli_app::Command> make_source_command() {
    return std::make_unique<SourceCommand>();
}

}  // namespace pv::app
