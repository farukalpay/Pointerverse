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
#include "pv/ingest/agent_audit_adapter.hpp"
#include "pv/ingest/ingestion_index.hpp"
#include "pv/ingest/ingestion_pipeline.hpp"
#include "pv/ingest/jsonl_adapter.hpp"
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

class IngestCommand final : public cli_app::Command {
public:
    void register_with(CLI::App& app) override {
        auto* ingest = app.add_subcommand("ingest", "Ingest external evidence logs");
        ingest->require_subcommand(1);
        set_root(ingest);

        agent_log_ = ingest->add_subcommand("agent-log", "Ingest agent/tool JSONL evidence");
        agent_log_->add_option("events", events_path_, "Path to JSONL events")->required();
        agent_log_->add_option("--domain", domain_, "Audit domain")->default_val("agent_audit");
        agent_log_->add_option("--branch", branch_, "Branch to ingest into")->default_val("main");
        agent_log_->add_option("--mode", mode_, "observe | strict")->default_val("observe");
        agent_log_->add_option("--store", store_path_, "Repository path")->default_val(".pvstore");
    }

    int run() override {
        if (!agent_log_->parsed()) {
            return EXIT_SUCCESS;
        }
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

private:
    CLI::App* agent_log_{nullptr};
    std::string store_path_{".pvstore"};
    std::string events_path_;
    std::string domain_{"agent_audit"};
    std::string branch_{"main"};
    std::string mode_{"observe"};
};

}  // namespace

std::unique_ptr<cli_app::Command> make_ingest_command() {
    return std::make_unique<IngestCommand>();
}

}  // namespace pv::app
