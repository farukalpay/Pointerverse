// SPDX-License-Identifier: Apache-2.0
#include "commands.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "command_utils.hpp"
#include "pv/audit/report.hpp"
#include "pv/audit/timeline.hpp"
#include "pv/storage/repository.hpp"

namespace pv::app {
namespace {

class AuditCommand final : public cli_app::Command {
public:
    void register_with(CLI::App& app) override {
        auto* audit = app.add_subcommand("audit", "Audit report commands");
        audit->require_subcommand(1);
        set_root(audit);

        report_ = audit->add_subcommand("report", "Show audit report");
        report_->add_option("branch", report_branch_, "Branch name")->required();
        report_->add_option("--format", report_format_, "text | json")->default_val("text");
        report_->add_option("--store", store_path_, "Repository path")->default_val(".pvstore");

        violations_ = audit->add_subcommand("violations", "Show audit violations");
        violations_->add_option("branch", violations_branch_, "Branch name")->required();
        violations_->add_option("--store", store_path_, "Repository path")->default_val(".pvstore");

        timeline_ = audit->add_subcommand("timeline", "Show object audit timeline");
        timeline_->add_option("branch", timeline_branch_, "Branch name")->required();
        timeline_->add_option("object", timeline_object_, "Object name")->required();
        timeline_->add_option("--store", store_path_, "Repository path")->default_val(".pvstore");

        export_ = audit->add_subcommand("export", "Export audit report");
        export_->add_option("branch", export_branch_, "Branch name")->required();
        export_->add_option("--format", export_format_, "json")->default_val("json");
        export_->add_option("--store", store_path_, "Repository path")->default_val(".pvstore");

        first_broke_ = audit->add_subcommand("first-broke", "Find the first commit that broke a law");
        first_broke_->add_option("branch", first_broke_branch_, "Branch name")->required();
        first_broke_->add_option("law", first_broke_law_, "Law name")->required();
        first_broke_->add_option("--store", store_path_, "Repository path")->default_val(".pvstore");
    }

    int run() override {
        if (report_->parsed()) {
            return run_checked([&] {
                const auto repository = Repository::open(store_path_);
                const auto report = AuditReportGenerator{}.generate(repository, report_branch_);
                if (report_format_ == "text") {
                    std::cout << render_audit_report_text(report);
                    return EXIT_SUCCESS;
                }
                if (report_format_ == "json") {
                    std::cout << render_audit_report_json(report);
                    return EXIT_SUCCESS;
                }
                throw std::invalid_argument("format must be text or json");
            });
        }
        if (violations_->parsed()) {
            return run_checked([&] {
                const auto repository = Repository::open(store_path_);
                const auto report = AuditReportGenerator{}.generate(repository, violations_branch_);
                std::cout << render_audit_violations_text(report);
                return EXIT_SUCCESS;
            });
        }
        if (timeline_->parsed()) {
            return run_checked([&] {
                const auto repository = Repository::open(store_path_);
                const auto entries = audit_timeline(repository, timeline_branch_, timeline_object_);
                std::cout << render_audit_timeline_text(timeline_branch_, timeline_object_, entries);
                return EXIT_SUCCESS;
            });
        }
        if (export_->parsed()) {
            return run_checked([&] {
                const auto repository = Repository::open(store_path_);
                const auto report = AuditReportGenerator{}.generate(repository, export_branch_);
                if (export_format_ != "json") {
                    throw std::invalid_argument("audit export supports only --format json");
                }
                std::cout << render_audit_report_json(report);
                return EXIT_SUCCESS;
            });
        }
        if (first_broke_->parsed()) {
            return run_checked([&] {
                const auto repository = Repository::open(store_path_);
                const auto report = AuditReportGenerator{}.generate(repository, first_broke_branch_);
                const auto violation = first_violation(report, first_broke_law_);
                std::cout << render_first_break_text(first_broke_branch_, first_broke_law_, violation);
                return EXIT_SUCCESS;
            });
        }
        return EXIT_SUCCESS;
    }

private:
    CLI::App* report_{nullptr};
    CLI::App* violations_{nullptr};
    CLI::App* timeline_{nullptr};
    CLI::App* export_{nullptr};
    CLI::App* first_broke_{nullptr};
    std::string store_path_{".pvstore"};
    std::string report_branch_;
    std::string report_format_{"text"};
    std::string violations_branch_;
    std::string timeline_branch_;
    std::string timeline_object_;
    std::string export_branch_;
    std::string export_format_{"json"};
    std::string first_broke_branch_;
    std::string first_broke_law_;
};

}  // namespace

std::unique_ptr<cli_app::Command> make_audit_command() {
    return std::make_unique<AuditCommand>();
}

}  // namespace pv::app
