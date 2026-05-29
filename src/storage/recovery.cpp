// SPDX-License-Identifier: Apache-2.0
#include "pv/storage/recovery.hpp"

#include <filesystem>
#include <utility>

namespace pv {

RepositoryRecovery::RepositoryRecovery(std::filesystem::path root, RepositoryEngine& engine, Wal& wal)
    : root_(std::move(root)), engine_(engine), wal_(wal) {}

RepositoryRecoveryReport RepositoryRecovery::recover(bool repair) const {
    RepositoryRecoveryReport report;
    const auto wal_report = wal_.recover();
    report.wal_entries = wal_report.entries_read;
    report.incomplete_wal = wal_report.incomplete_commit;

    const auto remove_temp_files = [&](const std::filesystem::path& dir) {
        if (!std::filesystem::exists(dir)) {
            return;
        }
        std::error_code ignored;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".tmp") {
                report.actions.push_back("removed temp file " + entry.path().string());
                if (repair) {
                    std::filesystem::remove(entry.path(), ignored);
                }
            }
        }
    };

    remove_temp_files(root_ / "refs");
    remove_temp_files(root_ / "history");
    remove_temp_files(root_ / "index");

    if (std::filesystem::exists(root_ / "staging")) {
        report.actions.push_back("removed staging directory " + (root_ / "staging").string());
        if (repair) {
            std::error_code ignored;
            std::filesystem::remove_all(root_ / "staging", ignored);
        }
    }

    const auto index_report = engine_.check_indexes();
    if (!index_report.clean) {
        for (const auto& message : index_report.messages) {
            report.actions.push_back("rebuilt index: " + message);
        }
        if (repair) {
            engine_.rebuild_indexes();
            report.repaired = true;
        }
    }

    if (report.incomplete_wal && repair) {
        wal_.truncate();
        report.actions.push_back("checkpointed incomplete WAL after index repair");
        report.incomplete_wal = false;
        report.repaired = true;
    }

    return report;
}

}  // namespace pv
