// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pv/hash/canonical.hpp"
#include "pv/runtime/commit_record.hpp"
#include "pv/storage/canonical_codec.hpp"

namespace pv {

struct StoredCommit {
    CommitRecord record;
    Hash256 before_snapshot_object;
    Hash256 after_snapshot_object;
    Hash256 delta_object;
    Hash256 trace_object;
    Hash256 law_status_object;
    Hash256 violation_object;
    Hash256 morphism_path_object;
};

[[nodiscard]] StoredCommit make_stored_commit(const CommitRecord& record);

void encode(CanonicalWriter& writer, const StoredCommit& commit);
[[nodiscard]] StoredCommit decode_stored_commit(CanonicalReader& reader);

}  // namespace pv
