// SPDX-License-Identifier: Apache-2.0
#include "pv/storage/object_codec.hpp"

#include <stdexcept>

#include "pv/hash/hasher.hpp"

namespace pv {

StoredCommit make_stored_commit(const CommitRecord& record) {
    return StoredCommit{
        record,
        record.before_hash,
        record.after_hash,
        record.delta_hash,
        record.program_hash,
        record.trace_hash,
        record.law_hash,
        record.violation_hash,
        record.morphism_path_hash,
        4
    };
}

CommitId stored_commit_identity(const StoredCommit& commit) {
    if (commit.format_version == 4) {
        return CommitId{sha256(canonical_encode_commit_identity(commit.record))};
    }
    if (commit.format_version == 3) {
        return CommitId{sha256(canonical_encode_commit_identity_v3(commit.record))};
    }
    return CommitId{};
}

void encode(CanonicalWriter& writer, const StoredCommit& commit) {
    writer.string("StoredCommit:v4");
    encode_commit_record_body_v4(writer, commit.record);
    writer.hash(commit.before_snapshot_object);
    writer.hash(commit.after_snapshot_object);
    writer.hash(commit.delta_object);
    writer.hash(commit.program_object);
    writer.hash(commit.trace_object);
    writer.hash(commit.law_status_object);
    writer.hash(commit.violation_object);
    writer.hash(commit.morphism_path_object);
}

StoredCommit decode_stored_commit(CanonicalReader& reader) {
    const auto tag = reader.string();
    if (tag != "StoredCommit:v1" && tag != "StoredCommit:v2" && tag != "StoredCommit:v3" && tag != "StoredCommit:v4") {
        throw std::runtime_error("canonical stream has unexpected type tag");
    }
    auto record = tag == "StoredCommit:v1"
        ? decode_commit_record_body_v1(reader)
        : (tag == "StoredCommit:v2"
            ? decode_commit_record_body_v2(reader)
            : (tag == "StoredCommit:v3" ? decode_commit_record_body(reader) : decode_commit_record_body_v4(reader)));
    StoredCommit commit = make_stored_commit(record);
    commit.format_version = tag == "StoredCommit:v4" ? 4 : (tag == "StoredCommit:v3" ? 3 : (tag == "StoredCommit:v2" ? 2 : 1));
    commit.before_snapshot_object = reader.hash();
    commit.after_snapshot_object = reader.hash();
    commit.delta_object = reader.hash();
    if (tag == "StoredCommit:v3" || tag == "StoredCommit:v4") {
        commit.program_object = reader.hash();
    }
    commit.trace_object = reader.hash();
    commit.law_status_object = reader.hash();
    commit.violation_object = reader.hash();
    commit.morphism_path_object = reader.hash();
    return commit;
}

}  // namespace pv
