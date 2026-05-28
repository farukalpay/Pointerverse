// SPDX-License-Identifier: Apache-2.0
#include "pv/runtime/commit_record.hpp"

#include "pv/hash/hasher.hpp"
#include "pv/storage/canonical_codec.hpp"

namespace pv {

CommitId make_commit_id(const CommitRecord& record) {
    return CommitId{sha256(canonical_encode_commit_identity(record))};
}

}  // namespace pv
