// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string_view>
#include <vector>

#include "pv/core/snapshot.hpp"
#include "pv/runtime/commit_record.hpp"

namespace pv {

class Repository;

class ProjectionStore {
public:
    explicit ProjectionStore(const Repository& repository);

    [[nodiscard]] const Repository& repository() const noexcept;
    [[nodiscard]] WorldSnapshot snapshot(std::string_view branch) const;
    [[nodiscard]] std::vector<CommitRecord> history(std::string_view branch) const;

private:
    const Repository& repository_;
};

}  // namespace pv
