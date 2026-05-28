// SPDX-License-Identifier: Apache-2.0
#include "pack_runner.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/wait.h>
#endif

#include <fmt/format.h>

#include "command_utils.hpp"
#include "pv/surface/registry.hpp"

namespace pv::app {

std::filesystem::path default_packs_root() {
    const auto cwd_root = std::filesystem::current_path() / "examples" / "packs";
    if (std::filesystem::exists(cwd_root)) {
        return cwd_root;
    }
    return source_root() / "examples" / "packs";
}

int run_pack(std::string_view id, const std::filesystem::path& packs_root) {
    const auto pack = find_pack(packs_root, id);
    if (!pack.has_value()) {
        throw std::runtime_error(fmt::format("unknown pack '{}'", id));
    }
    if (!std::filesystem::exists(pack->runner)) {
        throw std::runtime_error("pack runner not found: " + pack->runner.string());
    }
    const auto command = "POINTERVERSE_SOURCE_ROOT="
        + shell_quote(source_root())
        + " "
        + shell_quote(pack->runner);
    const auto status = std::system(command.c_str());
    if (status == -1) {
        return EXIT_FAILURE;
    }
#if defined(__unix__) || defined(__APPLE__)
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
#endif
    return status == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

}  // namespace pv::app
