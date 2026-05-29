// SPDX-License-Identifier: Apache-2.0
#include "pv/projection/projection_store.hpp"

#include "pv/storage/repository.hpp"

namespace pv {

ProjectionStore::ProjectionStore(const Repository& repository) : repository_(repository) {}

const Repository& ProjectionStore::repository() const noexcept {
    return repository_;
}

WorldSnapshot ProjectionStore::snapshot(std::string_view branch) const {
    return repository_.world(branch).snapshot();
}

std::vector<CommitRecord> ProjectionStore::history(std::string_view branch) const {
    return repository_.history(branch);
}

}  // namespace pv
