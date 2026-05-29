// SPDX-License-Identifier: Apache-2.0
#include "commands.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "command_utils.hpp"
#include "pv/domain/agent_audit.hpp"
#include "pv/hash/canonical.hpp"
#include "pv/measure/calibration.hpp"
#include "pv/measure/measurement_store.hpp"
#include "pv/measure/measurement_verifier.hpp"
#include "pv/measure/risk_functional.hpp"
#include "pv/measure/risk_projection.hpp"
#include "pv/rule/rule_engine.hpp"
#include "pv/runtime/transaction_types.hpp"
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
    std::vector<CommitRecord> history;
    for (const auto& record : repository.backend().history(branch)) {
        if (record.origin != TransactionOrigin::Internal) {
            history.push_back(record);
        }
    }
    if (history.empty()) {
        throw std::out_of_range("branch has no measurable commits");
    }
    if (prefix.empty() || prefix == "HEAD") {
        return history.back().id;
    }
    constexpr std::string_view head_prefix = "HEAD~";
    if (prefix.rfind(head_prefix, 0) == 0) {
        const auto offset = static_cast<std::size_t>(std::stoull(std::string{prefix.substr(head_prefix.size())}));
        if (offset >= history.size()) {
            throw std::out_of_range("HEAD offset is outside measurable branch history");
        }
        return history[history.size() - 1U - offset].id;
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
    json["commit_root"] = to_hex(measured.commit_root);
    json["spec_hash"] = to_hex(measured.spec_hash);
    json["risk"] = risk_json(measured.value);
    json["projection"] = measured.projection;
    json["evidence_root"] = to_hex(measured.evidence_root);
    json["measurement_object"] = to_hex(measured.measurement_object);
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
    output << "projection:   " << measured.projection << "\n";
    output << "spec hash:    " << to_hex(measured.spec_hash).substr(0, 12) << "\n";
    output << "evidence root:" << to_hex(measured.evidence_root).substr(0, 12) << "\n";
    output << "hash:         " << to_hex(measured.measurement_hash).substr(0, 12) << "\n";
    output << "\nevidence:\n";
    for (const auto& evidence : measured.evidence) {
        output << "  " << evidence.component << ":\n";
        output << "    " << evidence.explanation << "\n";
    }
    return output.str();
}

std::string render_branch_risk_text(
    std::string_view branch,
    const MeasurementBranchResult& measured,
    Hash256 spec_hash) {
    const auto risk = joined_risk(measured);
    const auto projection = project(risk, agent_audit_measurement_spec().projection);
    std::ostringstream output;
    output << "Measured risk: " << branch << "\n";
    output << "-------------------\n";
    output << "measurements:  " << measured.measured.size() << "\n";
    output << "cache hits:    " << measured.cache_hits << "\n";
    output << "cache misses:  " << measured.cache_misses << "\n";
    output << "structural:    " << risk.structural << "\n";
    output << "law:           " << risk.law_distance << "\n";
    output << "repair:        " << risk.repair_distance << "\n";
    output << "surprise:      " << risk.surprise << "\n";
    output << "projection:    " << projection << "\n";
    output << "spec hash:     " << to_hex(spec_hash).substr(0, 12) << "\n";
    return output.str();
}

std::string render_baseline_text(std::string_view name, const CalibrationBaseline& baseline) {
    std::ostringstream output;
    output << "Measurement baseline: " << name << "\n";
    output << "---------------------\n";
    output << "branch:       " << baseline.branch << "\n";
    output << "up to commit: " << short_hash(baseline.up_to_commit) << "\n";
    output << "sample:       " << baseline.sample.size() << "\n";
    output << "spec hash:    " << to_hex(baseline.spec_hash).substr(0, 12) << "\n";
    output << "hash:         " << to_hex(baseline.baseline_hash).substr(0, 12) << "\n";
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

        verify_ = measure->add_subcommand("verify", "Verify cached measurement ledger records");
        verify_->add_option("branch", verify_branch_, "Branch name")->required();
        verify_->add_option("--store", store_path_, "Repository path")->default_val(".pvstore");

        baseline_ = measure->add_subcommand("baseline", "Manage frozen measurement baselines");
        baseline_->require_subcommand(1);
        baseline_create_ = baseline_->add_subcommand("create", "Create or update a frozen measurement baseline");
        baseline_create_->add_option("branch", baseline_branch_, "Branch name")->required();
        baseline_create_->add_option("--up-to", baseline_up_to_, "Inclusive commit selector, e.g. HEAD~1")->required();
        baseline_create_->add_option("--store", store_path_, "Repository path")->default_val(".pvstore");
        baseline_show_ = baseline_->add_subcommand("show", "Show a frozen measurement baseline");
        baseline_show_->add_option("name", baseline_name_, "Baseline name")->required();
        baseline_show_->add_option("--store", store_path_, "Repository path")->default_val(".pvstore");

        cache_ = measure->add_subcommand("cache", "Manage measurement cache/index");
        cache_->require_subcommand(1);
        cache_rebuild_ = cache_->add_subcommand("rebuild", "Rebuild measurement cache for a branch");
        cache_rebuild_->add_option("branch", cache_branch_, "Branch name")->required();
        cache_rebuild_->add_option("--store", store_path_, "Repository path")->default_val(".pvstore");
    }

    int run() override {
        if (risk_->parsed()) {
            return run_checked([&] {
                auto repository = Repository::open(store_path_);
                auto verifier = agent_audit_verifier();
                const auto spec = agent_audit_measurement_spec();
                auto store = MeasurementStore{repository};
                const auto spec_hash = store.put_spec(spec);
                if (!risk_commit_.empty()) {
                    const auto commit = resolve_commit(repository, risk_branch_, risk_commit_);
                    const auto measured = store.measure_or_load_commit(risk_branch_, commit, spec, &verifier).measured;
                    if (risk_format_ == "text") {
                        std::cout << render_risk_text(risk_branch_, measured);
                        return EXIT_SUCCESS;
                    }
                    if (risk_format_ == "json") {
                        std::cout << measured_json(measured).dump(2) << "\n";
                        return EXIT_SUCCESS;
                    }
                    throw std::invalid_argument("format must be text or json");
                }
                const auto measured = store.measure_or_load_branch(risk_branch_, spec, &verifier);
                if (risk_format_ == "text") {
                    std::cout << render_branch_risk_text(risk_branch_, measured, spec_hash);
                    return EXIT_SUCCESS;
                }
                if (risk_format_ == "json") {
                    const auto joined = joined_risk(measured);
                    nlohmann::json json;
                    json["branch"] = risk_branch_;
                    json["spec_hash"] = to_hex(spec_hash);
                    json["measurements"] = measured.measured.size();
                    json["cache_hits"] = measured.cache_hits;
                    json["cache_misses"] = measured.cache_misses;
                    json["risk"] = risk_json(joined);
                    json["projection"] = project(joined, spec.projection);
                    json["measured_risks"] = nlohmann::json::array();
                    for (const auto& item : measured.measured) {
                        json["measured_risks"].push_back(measured_json(item));
                    }
                    std::cout << json.dump(2) << "\n";
                    return EXIT_SUCCESS;
                }
                throw std::invalid_argument("format must be text or json");
            });
        }
        if (explain_->parsed()) {
            return run_checked([&] {
                auto repository = Repository::open(store_path_);
                auto verifier = agent_audit_verifier();
                const auto spec = agent_audit_measurement_spec();
                const auto commit = resolve_commit(repository, explain_branch_, explain_commit_);
                const auto measured = MeasurementStore{repository}
                    .measure_or_load_commit(explain_branch_, commit, spec, &verifier)
                    .measured;
                std::cout << render_explain_text(explain_branch_, measured);
                return EXIT_SUCCESS;
            });
        }
        if (export_->parsed()) {
            return run_checked([&] {
                if (export_format_ != "json") {
                    throw std::invalid_argument("measure export supports only --format json");
                }
                auto repository = Repository::open(store_path_);
                auto verifier = agent_audit_verifier();
                const auto spec = agent_audit_measurement_spec();
                auto store = MeasurementStore{repository};
                const auto spec_hash = store.put_spec(spec);
                const auto measured = store.measure_or_load_branch(export_branch_, spec, &verifier);
                const auto joined = joined_risk(measured);
                nlohmann::json json;
                json["branch"] = export_branch_;
                json["spec_hash"] = to_hex(spec_hash);
                json["measurements"] = measured.measured.size();
                json["cache_hits"] = measured.cache_hits;
                json["cache_misses"] = measured.cache_misses;
                json["risk"] = risk_json(joined);
                json["projection"] = project(joined, spec.projection);
                json["measured_risks"] = nlohmann::json::array();
                for (const auto& item : measured.measured) {
                    json["measured_risks"].push_back(measured_json(item));
                }
                std::cout << json.dump(2) << "\n";
                return EXIT_SUCCESS;
            });
        }
        if (verify_->parsed()) {
            return run_checked([&] {
                auto repository = Repository::open(store_path_);
                auto verifier = agent_audit_verifier();
                const auto spec = agent_audit_measurement_spec();
                const auto report = MeasurementVerifier{repository}.verify_branch(verify_branch_, spec, &verifier);
                std::cout << render_measurement_verification_text(report, spec);
                return report.clean() ? EXIT_SUCCESS : EXIT_FAILURE;
            });
        }
        if (baseline_create_->parsed()) {
            return run_checked([&] {
                auto repository = Repository::open(store_path_);
                auto verifier = agent_audit_verifier();
                const auto spec = agent_audit_measurement_spec();
                const auto up_to = resolve_commit(repository, baseline_branch_, baseline_up_to_);
                const auto baseline = CalibrationStore{repository}.create(
                    baseline_branch_,
                    baseline_branch_,
                    up_to,
                    spec,
                    &verifier);
                std::cout << render_baseline_text(baseline_branch_, baseline);
                return EXIT_SUCCESS;
            });
        }
        if (baseline_show_->parsed()) {
            return run_checked([&] {
                auto repository = Repository::open(store_path_);
                const auto baseline = CalibrationStore{repository}.load(baseline_name_);
                std::cout << render_baseline_text(baseline_name_, baseline);
                return EXIT_SUCCESS;
            });
        }
        if (cache_rebuild_->parsed()) {
            return run_checked([&] {
                auto repository = Repository::open(store_path_);
                auto verifier = agent_audit_verifier();
                const auto spec = agent_audit_measurement_spec();
                auto store = MeasurementStore{repository};
                const auto spec_hash = store.put_spec(spec);
                const auto measured = store.rebuild_cache(cache_branch_, spec, &verifier);
                std::cout << render_branch_risk_text(cache_branch_, measured, spec_hash);
                return EXIT_SUCCESS;
            });
        }
        return EXIT_SUCCESS;
    }

private:
    CLI::App* risk_{nullptr};
    CLI::App* explain_{nullptr};
    CLI::App* export_{nullptr};
    CLI::App* verify_{nullptr};
    CLI::App* baseline_{nullptr};
    CLI::App* baseline_create_{nullptr};
    CLI::App* baseline_show_{nullptr};
    CLI::App* cache_{nullptr};
    CLI::App* cache_rebuild_{nullptr};
    std::string store_path_{".pvstore"};
    std::string risk_branch_;
    std::string risk_commit_;
    std::string risk_format_{"text"};
    std::string explain_branch_;
    std::string explain_commit_;
    std::string export_branch_;
    std::string export_format_{"json"};
    std::string verify_branch_;
    std::string baseline_branch_;
    std::string baseline_up_to_;
    std::string baseline_name_;
    std::string cache_branch_;
};

}  // namespace

std::unique_ptr<cli_app::Command> make_measure_command() {
    return std::make_unique<MeasureCommand>();
}

}  // namespace pv::app
