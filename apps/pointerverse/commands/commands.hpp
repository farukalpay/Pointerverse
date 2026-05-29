// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>

#include "pv/cli_app/command.hpp"
#include "pv/cli_app/registry.hpp"

namespace pv::app {

std::unique_ptr<cli_app::Command> make_world_command();
std::unique_ptr<cli_app::Command> make_repo_command();
std::unique_ptr<cli_app::Command> make_trace_command();
std::unique_ptr<cli_app::Command> make_source_command();
std::unique_ptr<cli_app::Command> make_ingest_command();
std::unique_ptr<cli_app::Command> make_audit_command();
std::unique_ptr<cli_app::Command> make_measure_command();
std::unique_ptr<cli_app::Command> make_project_command();
std::unique_ptr<cli_app::Command> make_decide_command();
std::unique_ptr<cli_app::Command> make_guard_command();
std::unique_ptr<cli_app::Command> make_sentinel_command();
std::unique_ptr<cli_app::Command> make_surface_command();
std::unique_ptr<cli_app::Command> make_pack_command();
std::unique_ptr<cli_app::Command> make_breakpoint_command();

void register_pointerverse_commands(cli_app::CommandRegistry& registry);

}  // namespace pv::app
