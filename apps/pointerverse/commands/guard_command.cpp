// SPDX-License-Identifier: Apache-2.0
#include "commands.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "command_utils.hpp"
#include "pv/guard/guard_pipeline.hpp"

namespace pv::app {
namespace {

class GuardCommand final : public cli_app::Command {
public:
    void register_with(CLI::App& app) override {
        auto* guard = app.add_subcommand("guard", "Audit git diffs for PR risk");
        guard->require_subcommand(1);
        set_root(guard);

        run_ = guard->add_subcommand("run", "Run Pointerverse Guard on a repository diff");
        run_->add_option("--repo", repo_path_, "Repository or working tree path")->default_val(".");
        run_->add_option("--base", base_, "Base git ref or baseline directory")->default_val("origin/main");
        run_->add_option("--head", head_, "Head git ref")->default_val("HEAD");
        run_->add_option("--mode", mode_, "observe | strict")->default_val("observe");
        run_->add_option("--format", format_, "text | json | markdown | sarif")->default_val("text");
        run_->add_option("--baseline", baseline_, "Frozen measurement baseline name for strict calibration");
        out_option_ = run_->add_option("--out", out_path_, "Report output path");
        markdown_out_option_ = run_->add_option("--markdown-out", markdown_out_path_, "Markdown report output path");
        json_out_option_ = run_->add_option("--json-out", json_out_path_, "JSON report output path");
        sarif_out_option_ = run_->add_option("--sarif-out", sarif_out_path_, "SARIF report output path");
        run_->add_option("--store", store_path_, "Pointerverse store path; defaults to <repo>/.pvstore");
    }

    int run() override {
        if (!run_->parsed()) {
            return EXIT_SUCCESS;
        }
        return run_checked([&] {
            GuardRunOptions options;
            options.repo = repo_path_;
            options.base = base_;
            options.head = head_;
            options.mode = mode_;
            options.format = format_;
            options.baseline = baseline_;
            options.store = store_path_;
            options.out = out_path_;
            options.markdown_out = markdown_out_path_;
            options.json_out = json_out_path_;
            options.sarif_out = sarif_out_path_;
            const auto explicit_outputs =
                out_option_->count() > 0
                || markdown_out_option_->count() > 0
                || json_out_option_->count() > 0
                || sarif_out_option_->count() > 0;
            options.write_default_artifacts = !explicit_outputs;
            const auto result = run_guard(options);
            if (!explicit_outputs) {
                std::cout << render_guard_report(result.report, format_);
            }
            return result.strict_failed ? EXIT_FAILURE : EXIT_SUCCESS;
        });
    }

private:
    CLI::App* run_{nullptr};
    CLI::Option* out_option_{nullptr};
    CLI::Option* markdown_out_option_{nullptr};
    CLI::Option* json_out_option_{nullptr};
    CLI::Option* sarif_out_option_{nullptr};
    std::string repo_path_{"."};
    std::string base_{"origin/main"};
    std::string head_{"HEAD"};
    std::string mode_{"observe"};
    std::string format_{"text"};
    std::string baseline_;
    std::string out_path_;
    std::string markdown_out_path_;
    std::string json_out_path_;
    std::string sarif_out_path_;
    std::string store_path_;
};

}  // namespace

std::unique_ptr<cli_app::Command> make_guard_command() {
    return std::make_unique<GuardCommand>();
}

}  // namespace pv::app
