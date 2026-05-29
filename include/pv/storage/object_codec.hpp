// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pv/hash/canonical.hpp"
#include "pv/runtime/commit_record.hpp"
#include "pv/kernel/canonical_codec.hpp"

namespace pv {

struct StoredCommit {
    CommitRecord record;
    Hash256 before_snapshot_object;
    Hash256 after_snapshot_object;
    Hash256 delta_object;
    Hash256 program_object;
    Hash256 trace_object;
    Hash256 law_status_object;
    Hash256 violation_object;
    Hash256 morphism_path_object;
    std::uint8_t format_version{4};
};

[[nodiscard]] StoredCommit make_stored_commit(const CommitRecord& record);
[[nodiscard]] CommitId stored_commit_identity(const StoredCommit& commit);

void encode(CanonicalWriter& writer, const StoredCommit& commit);
[[nodiscard]] StoredCommit decode_stored_commit(CanonicalReader& reader);

}  // namespace pv
