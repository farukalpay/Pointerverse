// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pv/hash/canonical.hpp"

namespace pv {

struct CommitProof {
    Hash256 before_root;
    Hash256 operation_root;
    Hash256 read_set_root;
    Hash256 write_set_root;
    Hash256 law_input_root;
    Hash256 law_output_root;
    Hash256 after_root;
};

[[nodiscard]] bool operator==(const CommitProof& left, const CommitProof& right) noexcept;
[[nodiscard]] Hash256 hash_commit_proof(const CommitProof& proof);

}  // namespace pv
