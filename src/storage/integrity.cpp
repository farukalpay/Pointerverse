// SPDX-License-Identifier: Apache-2.0
#include "pv/storage/integrity.hpp"

#include <utility>

#include "pv/sentinel/patrol.hpp"
#include "pv/storage/repository.hpp"

namespace pv {

IntegrityReport IntegrityChecker::check_repository(const Repository& repo) const {
    const auto sentinel = patrol_repository(repo);

    IntegrityReport report;
    report.commits_checked = sentinel.commits_checked;
    report.snapshots_checked = sentinel.snapshots_checked;
    report.objects_checked = sentinel.objects_checked;
    report.branch_refs_checked = sentinel.branch_refs_checked;
    for (const auto& issue : sentinel.issues) {
        if (issue.error) {
            report.errors.push_back(IntegrityError{issue.message});
        } else {
            report.warnings.push_back(IntegrityWarning{issue.message});
        }
    }
    return report;
}

}  // namespace pv
