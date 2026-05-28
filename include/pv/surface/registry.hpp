// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

#include "pv/surface/manifest.hpp"

namespace pv {

[[nodiscard]] std::vector<SurfaceManifest> built_in_surfaces();
[[nodiscard]] std::optional<SurfaceManifest> find_surface(std::string_view id);

[[nodiscard]] std::vector<PackManifest> discover_packs(const std::filesystem::path& packs_root);
[[nodiscard]] std::optional<PackManifest> find_pack(
    const std::filesystem::path& packs_root,
    std::string_view id);
[[nodiscard]] PackManifest read_pack_manifest(const std::filesystem::path& pack_root);

}  // namespace pv
