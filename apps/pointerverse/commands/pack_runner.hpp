// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <string_view>

namespace pv::app {

[[nodiscard]] std::filesystem::path default_packs_root();
[[nodiscard]] int run_pack(std::string_view id, const std::filesystem::path& packs_root = default_packs_root());

}  // namespace pv::app
