// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>

#include "pv/core/snapshot.hpp"
#include "pv/core/snapshot_page.hpp"
#include "pv/storage/content_store.hpp"

namespace pv {

class ChunkedSnapshotStore {
public:
    explicit ChunkedSnapshotStore(ContentStore& objects, std::size_t page_size = default_snapshot_page_size);

    [[nodiscard]] Hash256 put_snapshot(const WorldSnapshot& snapshot) const;
    [[nodiscard]] WorldSnapshot get_snapshot(Hash256 root_object) const;
    [[nodiscard]] ChunkedSnapshotPlan plan(const WorldSnapshot& snapshot) const;

private:
    ContentStore& objects_;
    std::size_t page_size_{default_snapshot_page_size};
};

}  // namespace pv
