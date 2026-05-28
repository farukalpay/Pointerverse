// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace pv {

class Repository;

struct IntegrityError {
    std::string message;
};

struct IntegrityWarning {
    std::string message;
};

struct IntegrityReport {
    std::size_t commits_checked{0};
    std::size_t snapshots_checked{0};
    std::size_t objects_checked{0};
    std::size_t branch_refs_checked{0};
    std::vector<IntegrityError> errors;
    std::vector<IntegrityWarning> warnings;

    [[nodiscard]] bool clean() const noexcept { return errors.empty(); }
};

class IntegrityChecker {
public:
    [[nodiscard]] IntegrityReport check_repository(const Repository& repo) const;
};

}  // namespace pv
