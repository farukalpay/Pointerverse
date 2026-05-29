// SPDX-License-Identifier: Apache-2.0
#include "pv/measure/risk_evidence.hpp"

#include <algorithm>

#include "pv/hash/hasher.hpp"
#include "pv/kernel/canonical_codec.hpp"

namespace pv {
namespace {

void sort_objects(std::vector<ObjectId>& ids) {
    std::ranges::sort(ids, [](ObjectId left, ObjectId right) {
        if (left.index != right.index) {
            return left.index < right.index;
        }
        return left.generation < right.generation;
    });
    ids.erase(std::ranges::unique(ids).begin(), ids.end());
}

void sort_pointers(std::vector<PointerId>& ids) {
    std::ranges::sort(ids, {}, &PointerId::value);
    ids.erase(std::ranges::unique(ids).begin(), ids.end());
}

void sort_commits(std::vector<CommitId>& ids) {
    std::ranges::sort(ids, [](const CommitId& left, const CommitId& right) {
        return to_hex(left.value) < to_hex(right.value);
    });
    ids.erase(std::ranges::unique(ids).begin(), ids.end());
}

void sort_laws(std::vector<LawId>& ids) {
    std::ranges::sort(ids);
    ids.erase(std::ranges::unique(ids).begin(), ids.end());
}

void write_object(CanonicalWriter& writer, ObjectId id) {
    writer.u32(id.index);
    writer.u32(id.generation);
}

void write_pointer(CanonicalWriter& writer, PointerId id) {
    writer.u64(id.value);
}

void write_commit(CanonicalWriter& writer, CommitId id) {
    writer.hash(id.value);
}

}  // namespace

Hash256 risk_evidence_hash(RiskEvidence evidence) {
    sort_objects(evidence.objects);
    sort_pointers(evidence.pointers);
    sort_commits(evidence.commits);
    sort_laws(evidence.laws);

    CanonicalWriter writer;
    writer.string("RiskEvidence:v1");
    writer.string(evidence.component);
    writer.hash(evidence.input_root);
    writer.hash(evidence.output_root);
    writer.u64(evidence.objects.size());
    for (const auto object : evidence.objects) {
        write_object(writer, object);
    }
    writer.u64(evidence.pointers.size());
    for (const auto pointer : evidence.pointers) {
        write_pointer(writer, pointer);
    }
    writer.u64(evidence.commits.size());
    for (const auto commit : evidence.commits) {
        write_commit(writer, commit);
    }
    writer.u64(evidence.laws.size());
    for (const auto& law : evidence.laws) {
        writer.string(law);
    }
    writer.string(evidence.explanation);
    return sha256(writer.bytes());
}

}  // namespace pv

