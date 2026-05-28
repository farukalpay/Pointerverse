// SPDX-License-Identifier: Apache-2.0
#include "pv/runtime/commit_record.hpp"

#include "pv/hash/hasher.hpp"

namespace pv {

CommitId make_commit_id(const CommitRecord& record) {
    CanonicalHasher hasher;
    hasher.write_string("CommitRecord:v1");
    hasher.write_u8(record.parent.has_value() ? 1 : 0);
    if (record.parent.has_value()) {
        hasher.write_hash(record.parent->value);
    }
    hasher.write_u64(record.parents.size());
    for (const auto& parent : record.parents) {
        hasher.write_hash(parent.value);
    }
    hasher.write_u64(record.world.value);
    hasher.write_u64(record.branch.value);
    hasher.write_string(record.branch_name);
    hasher.write_u64(record.transaction.value);
    hasher.write_u64(record.before_epoch.value);
    hasher.write_u64(record.after_epoch.value);
    hasher.write_u64(record.before_snapshot.value);
    hasher.write_u64(record.after_snapshot.value);
    hasher.write_hash(record.before_hash);
    hasher.write_hash(record.after_hash);
    hasher.write_hash(record.delta_hash);
    hasher.write_hash(record.trace_hash);
    hasher.write_hash(record.law_hash);
    hasher.write_hash(record.violation_hash);
    hasher.write_hash(record.morphism_path_hash);
    hasher.write_u8(record.accepted ? 1 : 0);
    hasher.write_u8(static_cast<std::uint8_t>(record.origin));
    hasher.write_string(record.label);
    return CommitId{hasher.finish()};
}

}  // namespace pv
