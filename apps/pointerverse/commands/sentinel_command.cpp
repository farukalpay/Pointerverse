// SPDX-License-Identifier: Apache-2.0
#include "commands.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#include <fmt/format.h>

#include "command_utils.hpp"
#include "pv/sentinel/boot_gate.hpp"
#include "pv/sentinel/fault_injection.hpp"
#include "pv/sentinel/sentinel.hpp"

namespace pv::app {
namespace {

std::chrono::seconds parse_interval(std::string_view value) {
    if (value.empty()) {
        throw std::invalid_argument("interval cannot be empty");
    }
    std::string text{value};
    if (text.ends_with('s')) {
        text.pop_back();
    }
    const auto seconds = std::stoull(text);
    if (seconds == 0) {
        throw std::invalid_argument("interval must be greater than zero seconds");
    }
    return std::chrono::seconds{seconds};
}

void print_fault_result(const FaultInjectionResult& result) {
    std::cout << "Pointerverse Sentinel Fault\n";
    std::cout << "---------------------------\n";
    std::cout << fmt::format("mutated: {}\n", result.mutated ? "yes" : "no");
    std::cout << fmt::format("target:  {}\n", result.target);
    std::cout << fmt::format("status:  {}\n", result.message);
}

class SentinelCommand final : public cli_app::Command {
public:
    void register_with(CLI::App& app) override {
        auto* sentinel = app.add_subcommand("sentinel", "Store and proof self-verification runtime");
        sentinel->require_subcommand(1);
        set_root(sentinel);

        boot_ = sentinel->add_subcommand("boot", "Run staged sentinel boot verification");
        boot_->add_option("store", store_path_, "Repository path")->default_val(".pvstore");

        patrol_ = sentinel->add_subcommand("patrol", "Run sentinel patrol workers");
        patrol_->add_option("store", store_path_, "Repository path")->default_val(".pvstore");
        once_option_ = patrol_->add_flag("--once", "Run exactly one patrol pass");
        every_option_ = patrol_->add_option("--every", patrol_every_, "Run patrol repeatedly at an interval such as 5s");

        report_ = sentinel->add_subcommand("report", "Show the latest sentinel report");
        report_->add_option("store", store_path_, "Repository path")->default_val(".pvstore");

        fault_ = sentinel->add_subcommand("fault", "Controlled sentinel fault injection");
        fault_->require_subcommand(1);
        corrupt_ = fault_->add_subcommand("corrupt-object", "Corrupt one object blob by kind");
        corrupt_->add_option("store", store_path_, "Repository path")->default_val(".pvstore");
        corrupt_->add_option("--kind", fault_kind_, "snapshot | commit | program | delta | trace | law | proof")->default_val("snapshot");
        corrupt_->add_option("--commit", fault_commit_, "Commit hash, prefix, or HEAD")->default_val("HEAD");
        corrupt_->add_option("--branch", fault_branch_, "Branch for HEAD/prefix resolution");
        corrupt_->add_flag("--yes-i-know-this-mutates-store", fault_confirm_, "Confirm destructive fault injection");

        flip_ = fault_->add_subcommand("flip-proof", "Rewrite the branch head with invalid proof roots");
        flip_->add_option("store", store_path_, "Repository path")->default_val(".pvstore");
        flip_->add_option("--commit", fault_commit_, "Commit hash, prefix, or HEAD")->default_val("HEAD");
        flip_->add_option("--branch", fault_branch_, "Branch for HEAD/prefix resolution");
        flip_->add_flag("--yes-i-know-this-mutates-store", fault_confirm_, "Confirm destructive fault injection");

        remove_program_ = fault_->add_subcommand("remove-program", "Remove a program object");
        remove_program_->add_option("store", store_path_, "Repository path")->default_val(".pvstore");
        remove_program_->add_option("--commit", fault_commit_, "Commit hash, prefix, or HEAD")->default_val("HEAD");
        remove_program_->add_option("--branch", fault_branch_, "Branch for HEAD/prefix resolution");
        remove_program_->add_flag("--yes-i-know-this-mutates-store", fault_confirm_, "Confirm destructive fault injection");

        rewrite_ref_ = fault_->add_subcommand("rewrite-ref", "Rewrite a branch ref to missing objects");
        rewrite_ref_->add_option("store", store_path_, "Repository path")->default_val(".pvstore");
        rewrite_ref_->add_option("--branch", fault_branch_, "Branch to rewrite")->default_val("main");
        rewrite_ref_->add_flag("--yes-i-know-this-mutates-store", fault_confirm_, "Confirm destructive fault injection");
    }

    int run() override {
        if (boot_->parsed()) {
            return run_checked([&] {
                const auto result = run_boot_gate(store_path_);
                std::cout << render_boot_gate_result(result);
                return result.ok ? EXIT_SUCCESS : EXIT_FAILURE;
            });
        }
        if (patrol_->parsed()) {
            return run_checked([&] {
                SentinelRuntime runtime{store_path_};
                if (every_option_->count() > 0) {
                    const auto interval = parse_interval(patrol_every_);
                    while (true) {
                        const auto result = runtime.tick();
                        std::cout << render_sentinel_report(result);
                        std::cout.flush();
                        std::this_thread::sleep_for(interval);
                    }
                }
                if (once_option_->count() == 0 && every_option_->count() == 0) {
                    throw std::invalid_argument("usage: sentinel patrol STORE --once or --every 5s");
                }
                const auto result = runtime.tick();
                std::cout << render_sentinel_report(result);
                return result.clean() ? EXIT_SUCCESS : EXIT_FAILURE;
            });
        }
        if (report_->parsed()) {
            return run_checked([&] {
                BootGateResult boot;
                (void)open_repository_with_sentinel(store_path_, &boot);
                SentinelRuntime runtime{store_path_};
                auto report = runtime.tick();
                report.measurement = boot.measurement.root;
                std::cout << render_sentinel_report(report);
                return report.clean() ? EXIT_SUCCESS : EXIT_FAILURE;
            });
        }
        if (corrupt_->parsed() || flip_->parsed() || remove_program_->parsed() || rewrite_ref_->parsed()) {
            return run_checked([&] {
                FaultInjectionOptions options;
                options.root = store_path_;
                options.branch = fault_branch_;
                options.commit = fault_commit_;
                options.kind = parse_fault_object_kind(fault_kind_);
                options.confirm_mutates_store = fault_confirm_;

                FaultInjectionResult result;
                if (corrupt_->parsed()) {
                    result = corrupt_object_fault(options);
                } else if (flip_->parsed()) {
                    result = flip_proof_fault(options);
                } else if (remove_program_->parsed()) {
                    result = remove_program_fault(options);
                } else {
                    result = rewrite_ref_fault(options);
                }
                print_fault_result(result);
                return EXIT_SUCCESS;
            });
        }
        return EXIT_SUCCESS;
    }

private:
    CLI::App* boot_{nullptr};
    CLI::App* patrol_{nullptr};
    CLI::App* report_{nullptr};
    CLI::App* fault_{nullptr};
    CLI::App* corrupt_{nullptr};
    CLI::App* flip_{nullptr};
    CLI::App* remove_program_{nullptr};
    CLI::App* rewrite_ref_{nullptr};
    CLI::Option* once_option_{nullptr};
    CLI::Option* every_option_{nullptr};
    std::string store_path_{".pvstore"};
    std::string patrol_every_;
    std::string fault_kind_{"snapshot"};
    std::string fault_commit_{"HEAD"};
    std::string fault_branch_;
    bool fault_confirm_{false};
};

}  // namespace

std::unique_ptr<cli_app::Command> make_sentinel_command() {
    return std::make_unique<SentinelCommand>();
}

}  // namespace pv::app
