// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace pv {

struct SurfaceManifest {
    std::string id;
    std::string title;
    std::string one_liner;
    std::vector<std::string> commands;
    std::vector<std::string> examples;
};

struct PackManifest {
    std::string id;
    std::string title;
    std::string surface;
    std::filesystem::path root;
    std::filesystem::path entry;
    std::filesystem::path runner;
    std::filesystem::path expected;
    std::filesystem::path readme;
};

}  // namespace pv
