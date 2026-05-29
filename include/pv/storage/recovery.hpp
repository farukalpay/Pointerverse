// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "pv/storage/repository_engine.hpp"

namespace pv {

struct RepositoryRecoveryReport {
    std::size_t wal_entries{0};
    bool incomplete_wal{false};
    bool repaired{false};
    std::vector<std::string> actions;

    [[nodiscard]] bool clean() const noexcept { return !incomplete_wal && actions.empty(); }
};

class RepositoryRecovery {
public:
    RepositoryRecovery(std::filesystem::path root, RepositoryEngine& engine, Wal& wal);

    [[nodiscard]] RepositoryRecoveryReport recover(bool repair) const;

private:
    std::filesystem::path root_;
    RepositoryEngine& engine_;
    Wal& wal_;
};

}  // namespace pv
