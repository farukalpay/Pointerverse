// SPDX-License-Identifier: Apache-2.0
#include "pv/storage/object_codec.hpp"

#include <stdexcept>

namespace pv {

StoredCommit make_stored_commit(const CommitRecord& record) {
    return StoredCommit{
        record,
        record.before_hash,
        record.after_hash,
        record.delta_hash,
        record.trace_hash,
        record.law_hash,
        record.violation_hash,
        record.morphism_path_hash
    };
}

void encode(CanonicalWriter& writer, const StoredCommit& commit) {
    writer.string("StoredCommit:v2");
    encode_commit_record_body(writer, commit.record);
    writer.hash(commit.before_snapshot_object);
    writer.hash(commit.after_snapshot_object);
    writer.hash(commit.delta_object);
    writer.hash(commit.trace_object);
    writer.hash(commit.law_status_object);
    writer.hash(commit.violation_object);
    writer.hash(commit.morphism_path_object);
}

StoredCommit decode_stored_commit(CanonicalReader& reader) {
    const auto tag = reader.string();
    if (tag != "StoredCommit:v1" && tag != "StoredCommit:v2") {
        throw std::runtime_error("canonical stream has unexpected type tag");
    }
    auto record = tag == "StoredCommit:v1"
        ? decode_commit_record_body_v1(reader)
        : decode_commit_record_body(reader);
    StoredCommit commit = make_stored_commit(record);
    commit.before_snapshot_object = reader.hash();
    commit.after_snapshot_object = reader.hash();
    commit.delta_object = reader.hash();
    commit.trace_object = reader.hash();
    commit.law_status_object = reader.hash();
    commit.violation_object = reader.hash();
    commit.morphism_path_object = reader.hash();
    return commit;
}

}  // namespace pv
