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
#include "pv/domain/package.hpp"
#include "pv/ingest/agent_audit_adapter.hpp"
#include "pv/ingest/graph_log_importer.hpp"
#include "pv/ingest/ingestion_index.hpp"
#include "pv/ingest/ingestion_pipeline.hpp"
#include "pv/ingest/jsonl_adapter.hpp"
#include "pv/normalize/canonical_event.hpp"
#include "pv/normalize/event_normalizer.hpp"
#include "pv/normalize/graph_event_encoder.hpp"
#include "pv/rule/rule.hpp"
#include "pv/source/source_adapter.hpp"
#include "pv/storage/repository.hpp"

namespace pv::app {
namespace {

VerificationMode parse_ingestion_mode(std::string_view mode) {
    if (mode == "observe") {
        return VerificationMode::Observe;
    }
    if (mode == "strict") {
        return VerificationMode::Strict;
    }
    throw std::invalid_argument("mode must be observe or strict");
}

void print_ingestion_report(const IngestionResult& result) {
    std::cout << "Ingestion report\n";
    std::cout << "----------------\n";
    std::cout << fmt::format("events read:          {}\n", result.events_read);
    std::cout << fmt::format("accepted:             {}\n", result.accepted);
    std::cout << fmt::format("skipped duplicates:   {}\n", result.skipped_duplicates);
    std::cout << fmt::format("rejected:             {}\n", result.rejected);
    std::cout << fmt::format("errors:               {}\n", result.errors);
    std::cout << fmt::format("violations:           {}\n", result.violations);
    for (const auto& message : result.messages) {
        if (message.line > 0) {
            std::cout << fmt::format("error line {}: {}\n", message.line, message.message);
        } else if (!message.event_id.empty()) {
            std::cout << fmt::format("event {}: {}\n", message.event_id, message.message);
        } else {
            std::cout << fmt::format("error: {}\n", message.message);
        }
    }
}

void add_source_errors(IngestionResult& result, const SourceBatch& batch) {
    result.events_read += batch.errors.size();
    result.errors += batch.errors.size();
    for (const auto& error : batch.errors) {
        result.messages.push_back(IngestionMessage{error.line, error.event_id, error.message});
    }
}

std::vector<GraphEvent> graph_events_from_source_batch(
    const SourceBatch& batch,
    IngestionResult& result) {
    SourceEventNormalizer normalizer;
    GraphEventEncoder encoder;
    std::vector<GraphEvent> events;
    events.reserve(batch.events.size());
    for (const auto& event : batch.events) {
        try {
            events.push_back(encoder.encode(normalizer.normalize(event)));
        } catch (const std::exception& error) {
            result.errors += 1;
            result.messages.push_back(IngestionMessage{0, event.id, error.what()});
        }
    }
    return events;
}

std::vector<GraphEvent> read_canonical_graph_events(
    const std::string& path,
    IngestionResult& result) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error(fmt::format("cannot open events '{}'", path));
    }

    GraphEventEncoder encoder;
    std::vector<GraphEvent> events;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        line_number += 1;
        if (line.empty()) {
            continue;
        }
        try {
            events.push_back(encoder.encode(canonical_event_from_jsonl(line)));
        } catch (const std::exception& error) {
            result.events_read += 1;
            result.errors += 1;
            result.messages.push_back(IngestionMessage{line_number, {}, error.what()});
        }
    }
    return events;
}

std::vector<GraphEvent> read_graph_events(
    const std::string& path,
    IngestionResult& result) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error(fmt::format("cannot open events '{}'", path));
    }

    std::vector<GraphEvent> events;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        line_number += 1;
        if (line.empty()) {
            continue;
        }
        try {
            events.push_back(graph_event_from_jsonl(line));
        } catch (const std::exception& error) {
            result.events_read += 1;
            result.errors += 1;
            result.messages.push_back(IngestionMessage{line_number, {}, error.what()});
        }
    }
    return events;
}

std::vector<Rule> read_rules(const std::string& path) {
    std::vector<Rule> rules;
    if (path.empty()) {
        return rules;
    }
    std::ifstream rules_input(path);
    if (!rules_input) {
        throw std::runtime_error(fmt::format("cannot open rules '{}'", path));
    }
    std::ostringstream buffer;
    buffer << rules_input.rdbuf();
    return parse_domain_package(buffer.str()).rules;
}

class IngestCommand final : public cli_app::Command {
public:
    void register_with(CLI::App& app) override {
        auto* ingest = app.add_subcommand("ingest", "Ingest external evidence logs");
        ingest->require_subcommand(1);
        set_root(ingest);

        raw_ = ingest->add_subcommand("raw", "Ingest a raw source stream through an adapter");
        raw_->add_option("events", raw_events_path_, "Path to raw source events")->required();
        raw_->add_option("--adapter", raw_adapter_, "Source adapter")->default_val("jsonl");
        raw_->add_option("--branch", raw_branch_, "Branch to ingest into")->default_val("main");
        raw_->add_option("--mode", raw_mode_, "observe | strict")->default_val("observe");
        raw_->add_option("--rules", raw_rules_path_, "Optional domain package file with rules");
        raw_->add_option("--store", store_path_, "Repository path")->default_val(".pvstore");

        canon_ = ingest->add_subcommand("canon", "Ingest canonical event JSONL");
        canon_->add_option("events", canon_events_path_, "Path to canonical JSONL events")->required();
        canon_->add_option("--branch", canon_branch_, "Branch to ingest into")->default_val("main");
        canon_->add_option("--mode", canon_mode_, "observe | strict")->default_val("observe");
        canon_->add_option("--rules", canon_rules_path_, "Optional domain package file with rules");
        canon_->add_option("--store", store_path_, "Repository path")->default_val(".pvstore");

        graph_ = ingest->add_subcommand("graph", "Ingest graph event JSONL");
        graph_->add_option("events", graph_events_path_, "Path to graph JSONL events")->required();
        graph_->add_option("--branch", graph_branch_, "Branch to ingest into")->default_val("main");
        graph_->add_option("--mode", graph_mode_, "observe | strict")->default_val("observe");
        graph_->add_option("--rules", graph_rules_path_, "Optional domain package file with rules");
        graph_->add_option("--store", store_path_, "Repository path")->default_val(".pvstore");

        agent_log_ = ingest->add_subcommand("agent-log", "Ingest agent/tool JSONL evidence");
        agent_log_->add_option("events", events_path_, "Path to JSONL events")->required();
        agent_log_->add_option("--domain", domain_, "Audit domain")->default_val("agent_audit");
        agent_log_->add_option("--branch", branch_, "Branch to ingest into")->default_val("main");
        agent_log_->add_option("--mode", mode_, "observe | strict")->default_val("observe");
        agent_log_->add_option("--store", store_path_, "Repository path")->default_val(".pvstore");

        graph_log_ = ingest->add_subcommand("graph-log", "Ingest a generic graph-event JSONL stream");
        graph_log_->add_option("events", legacy_graph_events_path_, "Path to JSONL graph events")->required();
        graph_log_->add_option("--branch", legacy_graph_branch_, "Branch to ingest into")->default_val("main");
        graph_log_->add_option("--mode", legacy_graph_mode_, "observe | strict")->default_val("observe");
        graph_log_->add_option("--rules", legacy_graph_rules_path_, "Optional domain package file with rules");
        graph_log_->add_option("--store", store_path_, "Repository path")->default_val(".pvstore");
    }

    int run() override {
        if (raw_->parsed()) {
            return run_checked([&] {
                if (raw_adapter_ != "jsonl") {
                    throw std::invalid_argument("raw ingest adapter must be jsonl");
                }
                auto repository = Repository::open(store_path_);
                std::ifstream input(raw_events_path_);
                if (!input) {
                    throw std::runtime_error(fmt::format("cannot open events '{}'", raw_events_path_));
                }
                const auto batch = JsonlSourceAdapter{"jsonl"}.read(input);
                IngestionResult result;
                add_source_errors(result, batch);
                auto events = graph_events_from_source_batch(batch, result);

                IngestionIndex index{repository.root()};
                IngestionOptions options;
                options.branch = raw_branch_;
                options.mode = parse_ingestion_mode(raw_mode_);
                auto imported = GraphLogImporter{repository}.import(events, index, options, read_rules(raw_rules_path_));
                imported.events_read += result.events_read;
                imported.errors += result.errors;
                imported.messages.insert(imported.messages.end(), result.messages.begin(), result.messages.end());
                print_ingestion_report(imported);
                const auto ok = imported.errors == 0
                    && (options.mode == VerificationMode::Observe || imported.rejected == 0);
                return ok ? EXIT_SUCCESS : EXIT_FAILURE;
            });
        }
        if (canon_->parsed()) {
            return run_checked([&] {
                auto repository = Repository::open(store_path_);
                IngestionResult result;
                auto events = read_canonical_graph_events(canon_events_path_, result);

                IngestionIndex index{repository.root()};
                IngestionOptions options;
                options.branch = canon_branch_;
                options.mode = parse_ingestion_mode(canon_mode_);
                auto imported = GraphLogImporter{repository}.import(events, index, options, read_rules(canon_rules_path_));
                imported.events_read += result.events_read;
                imported.errors += result.errors;
                imported.messages.insert(imported.messages.end(), result.messages.begin(), result.messages.end());
                print_ingestion_report(imported);
                const auto ok = imported.errors == 0
                    && (options.mode == VerificationMode::Observe || imported.rejected == 0);
                return ok ? EXIT_SUCCESS : EXIT_FAILURE;
            });
        }
        if (graph_->parsed()) {
            return run_checked([&] {
                auto repository = Repository::open(store_path_);
                IngestionResult result;
                auto events = read_graph_events(graph_events_path_, result);

                IngestionIndex index{repository.root()};
                IngestionOptions options;
                options.branch = graph_branch_;
                options.mode = parse_ingestion_mode(graph_mode_);
                auto imported = GraphLogImporter{repository}.import(events, index, options, read_rules(graph_rules_path_));
                imported.events_read += result.events_read;
                imported.errors += result.errors;
                imported.messages.insert(imported.messages.end(), result.messages.begin(), result.messages.end());
                print_ingestion_report(imported);
                const auto ok = imported.errors == 0
                    && (options.mode == VerificationMode::Observe || imported.rejected == 0);
                return ok ? EXIT_SUCCESS : EXIT_FAILURE;
            });
        }
        if (agent_log_->parsed()) {
            return run_checked([&] {
                auto repository = Repository::open(store_path_);
                std::ifstream input(events_path_);
                if (!input) {
                    throw std::runtime_error(fmt::format("cannot open events '{}'", events_path_));
                }

                const JsonlEvidenceAdapter jsonl{"agent-log"};
                const auto batch = jsonl.read(input);
                AgentAuditAdapter normalizer;
                IngestionIndex index{repository.root()};
                IngestionOptions options;
                options.branch = branch_;
                options.domain = domain_;
                options.mode = parse_ingestion_mode(mode_);

                auto result = IngestionPipeline{repository}.ingest(batch.events, normalizer, index, options);
                result.events_read += batch.errors.size();
                result.errors += batch.errors.size();
                for (const auto& error : batch.errors) {
                    result.messages.push_back(IngestionMessage{error.line, {}, error.message});
                }
                print_ingestion_report(result);
                const auto ok = result.errors == 0
                    && (options.mode == VerificationMode::Observe || result.rejected == 0);
                return ok ? EXIT_SUCCESS : EXIT_FAILURE;
            });
        }
        if (graph_log_->parsed()) {
            return run_checked([&] {
                auto repository = Repository::open(store_path_);
                std::ifstream input(legacy_graph_events_path_);
                if (!input) {
                    throw std::runtime_error(fmt::format("cannot open events '{}'", legacy_graph_events_path_));
                }

                IngestionIndex index{repository.root()};
                IngestionOptions options;
                options.branch = legacy_graph_branch_;
                options.mode = parse_ingestion_mode(legacy_graph_mode_);

                const auto result = GraphLogImporter{repository}.import(input, index, options, read_rules(legacy_graph_rules_path_));
                print_ingestion_report(result);
                const auto ok = result.errors == 0
                    && (options.mode == VerificationMode::Observe || result.rejected == 0);
                return ok ? EXIT_SUCCESS : EXIT_FAILURE;
            });
        }
        return EXIT_SUCCESS;
    }

private:
    CLI::App* raw_{nullptr};
    CLI::App* canon_{nullptr};
    CLI::App* graph_{nullptr};
    CLI::App* agent_log_{nullptr};
    CLI::App* graph_log_{nullptr};
    std::string store_path_{".pvstore"};
    std::string raw_events_path_;
    std::string raw_adapter_{"jsonl"};
    std::string raw_branch_{"main"};
    std::string raw_mode_{"observe"};
    std::string raw_rules_path_;
    std::string canon_events_path_;
    std::string canon_branch_{"main"};
    std::string canon_mode_{"observe"};
    std::string canon_rules_path_;
    std::string graph_events_path_;
    std::string graph_branch_{"main"};
    std::string graph_mode_{"observe"};
    std::string graph_rules_path_;
    std::string events_path_;
    std::string domain_{"agent_audit"};
    std::string branch_{"main"};
    std::string mode_{"observe"};
    std::string legacy_graph_events_path_;
    std::string legacy_graph_branch_{"main"};
    std::string legacy_graph_mode_{"observe"};
    std::string legacy_graph_rules_path_;
};

}  // namespace

std::unique_ptr<cli_app::Command> make_ingest_command() {
    return std::make_unique<IngestCommand>();
}

}  // namespace pv::app
