// SPDX-License-Identifier: Apache-2.0
#include "pv/kernel/proof.hpp"

#include "pv/hash/hasher.hpp"
#include "pv/storage/canonical_codec.hpp"

namespace pv {

bool operator==(const CommitProof& left, const CommitProof& right) noexcept {
    return left.program_root == right.program_root
        && left.before_root == right.before_root
        && left.operation_root == right.operation_root
        && left.read_set_root == right.read_set_root
        && left.write_set_root == right.write_set_root
        && left.law_input_root == right.law_input_root
        && left.law_output_root == right.law_output_root
        && left.after_root == right.after_root;
}

Hash256 hash_commit_proof(const CommitProof& proof) {
    CanonicalWriter writer;
    writer.string("CommitProof:v2");
    writer.hash(proof.program_root);
    writer.hash(proof.before_root);
    writer.hash(proof.operation_root);
    writer.hash(proof.read_set_root);
    writer.hash(proof.write_set_root);
    writer.hash(proof.law_input_root);
    writer.hash(proof.law_output_root);
    writer.hash(proof.after_root);
    return sha256(writer.bytes());
}

}  // namespace pv
