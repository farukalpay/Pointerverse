// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <filesystem>
#include <set>

#include "pv/hash/canonical.hpp"
#include "pv/storage/repository_engine.hpp"

namespace pv {

struct ReachabilityReport {
    std::size_t reachable_objects{0};
    std::size_t unreachable_objects{0};
    std::size_t quarantined_objects{0};
};

class RepositoryGc {
public:
    RepositoryGc(std::filesystem::path root, RepositoryEngine& engine, ContentStore& objects);

    [[nodiscard]] ReachabilityReport mark() const;
    [[nodiscard]] ReachabilityReport quarantine_unreachable() const;
    void prune_quarantine() const;

private:
    [[nodiscard]] std::set<std::string> reachable() const;
    [[nodiscard]] std::set<std::string> loose_objects() const;
    [[nodiscard]] std::filesystem::path quarantine_path(Hash256 id) const;

    std::filesystem::path root_;
    RepositoryEngine& engine_;
    ContentStore& objects_;
};

}  // namespace pv
