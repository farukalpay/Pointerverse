// SPDX-License-Identifier: Apache-2.0
#include "commands.hpp"

namespace pv::app {

void register_pointerverse_commands(cli_app::CommandRegistry& registry) {
    registry.add(make_world_command());
    registry.add(make_repo_command());
    registry.add(make_trace_command());
    registry.add(make_ingest_command());
    registry.add(make_audit_command());
    registry.add(make_measure_command());
    registry.add(make_guard_command());
    registry.add(make_sentinel_command());
    registry.add(make_surface_command());
    registry.add(make_pack_command());
}

}  // namespace pv::app
