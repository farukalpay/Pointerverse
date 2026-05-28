// SPDX-License-Identifier: Apache-2.0
#include "pv/storage/manifest.hpp"

#include <fstream>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

namespace pv {

ManifestStore::ManifestStore(std::filesystem::path root) : root_(std::move(root)) {}

void ManifestStore::write(const RepositoryManifest& manifest) const {
    std::filesystem::create_directories(root_);
    const auto path = root_ / "manifest.json";
    const auto tmp = root_ / "manifest.json.tmp";
    std::ofstream output(tmp, std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot write repository manifest");
    }
    output << nlohmann::json{
        {"version", manifest.version},
        {"current_branch", manifest.current_branch}
    }.dump(2) << '\n';
    output.close();
    std::filesystem::rename(tmp, path);
}

RepositoryManifest ManifestStore::read() const {
    std::ifstream input(root_ / "manifest.json");
    if (!input) {
        throw std::runtime_error("cannot open repository manifest");
    }
    const auto json = nlohmann::json::parse(input);
    RepositoryManifest manifest;
    manifest.version = json.value("version", 1U);
    manifest.current_branch = json.value("current_branch", std::string{"main"});
    if (manifest.version != 1) {
        throw std::runtime_error("unsupported repository manifest version");
    }
    return manifest;
}

bool ManifestStore::exists() const {
    return std::filesystem::exists(root_ / "manifest.json");
}

}  // namespace pv
