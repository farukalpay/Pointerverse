// SPDX-License-Identifier: Apache-2.0
#include "commands.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "command_utils.hpp"
#include "pv/domain/agent_audit.hpp"
#include "pv/hash/canonical.hpp"
#include "pv/measure/risk_functional.hpp"
#include "pv/measure/risk_projection.hpp"
#include "pv/rule/rule_engine.hpp"
#include "pv/storage/repository.hpp"

namespace pv::app {
namespace {

std::string short_hash(CommitId id) {
    return to_hex(id.value).substr(0, 12);
}

Verifier agent_audit_verifier() {
    const auto package = make_agent_audit_domain();
    RuleEngine rules;
    rules.add_all(package.rules);
    Verifier verifier{VerificationMode::Observe};
    for (const auto& rule : rules.rules()) {
        verifier.add(rules.make_law(rule.name));
    }
    return verifier;
}

CommitId resolve_commit(const Repository& repository, std::string_view branch, std::string_view prefix) {
    const auto history = repository.backend().history(branch);
    if (history.empty()) {
        throw std::out_of_range("branch has no commits");
    }
    if (prefix.empty()) {
        return history.back().id;
    }
    for (const auto& record : history) {
        const auto full = to_hex(record.id.value);
        if (full.rfind(prefix, 0) == 0) {
            return record.id;
        }
    }
    throw std::out_of_range("commit not found on branch");
}

nlohmann::json risk_json(RiskVector risk) {
    return {
        {"structural", risk.structural},
        {"law_distance", risk.law_distance},
        {"repair_distance", risk.repair_distance},
        {"surprise", risk.surprise}
    };
}

nlohmann::json evidence_json(const RiskEvidence& evidence) {
    nlohmann::json json;
    json["component"] = evidence.component;
    json["input_root"] = to_hex(evidence.input_root);
    json["output_root"] = to_hex(evidence.output_root);
    json["objects"] = nlohmann::json::array();
    for (const auto object : evidence.objects) {
        json["objects"].push_back(to_string(object));
    }
    json["pointers"] = nlohmann::json::array();
    for (const auto pointer : evidence.pointers) {
        json["pointers"].push_back(to_string(pointer));
    }
    json["commits"] = nlohmann::json::array();
    for (const auto commit : evidence.commits) {
        json["commits"].push_back(to_hex(commit.value));
    }
    json["laws"] = evidence.laws;
    json["explanation"] = evidence.explanation;
    return json;
}

nlohmann::json measured_json(const MeasuredRisk& measured) {
    nlohmann::json json;
    json["commit"] = to_hex(measured.commit.value);
    json["risk"] = risk_json(measured.value);
    json["projection"] = project(measured.value);
    json["measurement_hash"] = to_hex(measured.measurement_hash);
    json["evidence"] = nlohmann::json::array();
    for (const auto& evidence : measured.evidence) {
        json["evidence"].push_back(evidence_json(evidence));
    }
    return json;
}

std::string render_risk_text(std::string_view branch, const MeasuredRisk& measured) {
    std::ostringstream output;
    output << "Measured risk: " << branch << "\n";
    output << "-------------------\n";
    output << "commit:       " << short_hash(measured.commit) << "\n";
    output << "structural:   " << measured.value.structural << "\n";
    output << "law:          " << measured.value.law_distance << "\n";
    output << "repair:       " << measured.value.repair_distance << "\n";
    output << "surprise:     " << measured.value.surprise << "\n";
    output << "projection:   " << project(measured.value) << "\n";
    output << "hash:         " << to_hex(measured.measurement_hash).substr(0, 12) << "\n";
    output << "\nevidence:\n";
    for (const auto& evidence : measured.evidence) {
        output << "  " << evidence.component << ":\n";
        output << "    " << evidence.explanation << "\n";
    }
    return output.str();
}

std::string render_explain_text(std::string_view branch, const MeasuredRisk& measured) {
    std::ostringstream output;
    output << render_risk_text(branch, measured);
    output << "\ncontent roots:\n";
    for (const auto& evidence : measured.evidence) {
        output << "  " << evidence.component << " input=" << to_hex(evidence.input_root).substr(0, 12)
               << " output=" << to_hex(evidence.output_root).substr(0, 12) << "\n";
    }
    return output.str();
}

class MeasureCommand final : public cli_app::Command {
public:
    void register_with(CLI::App& app) override {
        auto* measure = app.add_subcommand("measure", "Measure deterministic backend risk");
        measure->require_subcommand(1);
        set_root(measure);

        risk_ = measure->add_subcommand("risk", "Measure risk for a branch or commit");
        risk_->add_option("branch", risk_branch_, "Branch name")->required();
        risk_->add_option("--commit", risk_commit_, "Commit hash or prefix");
        risk_->add_option("--store", store_path_, "Repository path")->default_val(".pvstore");
        risk_->add_option("--format", risk_format_, "text | json")->default_val("text");

        explain_ = measure->add_subcommand("explain", "Explain measured risk evidence");
        explain_->add_option("branch", explain_branch_, "Branch name")->required();
        explain_->add_option("--commit", explain_commit_, "Commit hash or prefix")->required();
        explain_->add_option("--store", store_path_, "Repository path")->default_val(".pvstore");

        export_ = measure->add_subcommand("export", "Export measured branch risk");
        export_->add_option("branch", export_branch_, "Branch name")->required();
        export_->add_option("--format", export_format_, "json")->default_val("json");
        export_->add_option("--store", store_path_, "Repository path")->default_val(".pvstore");
    }

    int run() override {
        if (risk_->parsed()) {
            return run_checked([&] {
                const auto repository = Repository::open(store_path_);
                auto verifier = agent_audit_verifier();
                const auto commit = resolve_commit(repository, risk_branch_, risk_commit_);
                const auto measured = MeasuredRiskFunctional{}.measure_commit(repository, risk_branch_, commit, verifier);
                if (risk_format_ == "text") {
                    std::cout << render_risk_text(risk_branch_, measured);
                    return EXIT_SUCCESS;
                }
                if (risk_format_ == "json") {
                    std::cout << measured_json(measured).dump(2) << "\n";
                    return EXIT_SUCCESS;
                }
                throw std::invalid_argument("format must be text or json");
            });
        }
        if (explain_->parsed()) {
            return run_checked([&] {
                const auto repository = Repository::open(store_path_);
                auto verifier = agent_audit_verifier();
                const auto commit = resolve_commit(repository, explain_branch_, explain_commit_);
                const auto measured = MeasuredRiskFunctional{}.measure_commit(repository, explain_branch_, commit, verifier);
                std::cout << render_explain_text(explain_branch_, measured);
                return EXIT_SUCCESS;
            });
        }
        if (export_->parsed()) {
            return run_checked([&] {
                if (export_format_ != "json") {
                    throw std::invalid_argument("measure export supports only --format json");
                }
                const auto repository = Repository::open(store_path_);
                auto verifier = agent_audit_verifier();
                const auto measured = MeasuredRiskFunctional{}.measure_branch(repository, export_branch_, &verifier);
                const auto joined = joined_risk(measured);
                nlohmann::json json;
                json["branch"] = export_branch_;
                json["risk"] = risk_json(joined);
                json["projection"] = project(joined);
                json["measured_risks"] = nlohmann::json::array();
                for (const auto& item : measured) {
                    json["measured_risks"].push_back(measured_json(item));
                }
                std::cout << json.dump(2) << "\n";
                return EXIT_SUCCESS;
            });
        }
        return EXIT_SUCCESS;
    }

private:
    CLI::App* risk_{nullptr};
    CLI::App* explain_{nullptr};
    CLI::App* export_{nullptr};
    std::string store_path_{".pvstore"};
    std::string risk_branch_;
    std::string risk_commit_;
    std::string risk_format_{"text"};
    std::string explain_branch_;
    std::string explain_commit_;
    std::string export_branch_;
    std::string export_format_{"json"};
};

}  // namespace

std::unique_ptr<cli_app::Command> make_measure_command() {
    return std::make_unique<MeasureCommand>();
}

}  // namespace pv::app

