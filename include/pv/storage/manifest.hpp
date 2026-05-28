// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <string>

namespace pv {

struct RepositoryManifest {
    std::uint32_t version{1};
    std::string current_branch{"main"};
};

class ManifestStore {
public:
    explicit ManifestStore(std::filesystem::path root);

    void write(const RepositoryManifest& manifest) const;
    [[nodiscard]] RepositoryManifest read() const;
    [[nodiscard]] bool exists() const;

private:
    std::filesystem::path root_;
};

}  // namespace pv
