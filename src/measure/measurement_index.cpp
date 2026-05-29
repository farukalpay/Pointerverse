// SPDX-License-Identifier: Apache-2.0
#include "pv/measure/measurement_index.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "pv/hash/canonical.hpp"

namespace pv {
namespace {

void write_commit_id(IndexPayloadWriter& writer, CommitId id) {
    writer.hash(id.value);
}

CommitId read_commit_id(IndexPayloadReader& reader) {
    return CommitId{reader.hash()};
}

std::string key(const MeasurementIndexEntry& entry) {
    return entry.branch + ":" + to_hex(entry.commit.value) + ":" + to_hex(entry.spec_hash);
}

bool same_key(const MeasurementIndexEntry& left, const MeasurementIndexEntry& right) {
    return left.branch == right.branch
        && left.commit == right.commit
        && left.spec_hash == right.spec_hash;
}

void sort_entries(std::vector<MeasurementIndexEntry>& entries) {
    std::ranges::sort(entries, [](const auto& left, const auto& right) {
        return key(left) < key(right);
    });
}

std::vector<MeasurementIndexEntry> read_entries_v1(const std::vector<std::byte>& payload) {
    IndexPayloadReader reader{payload};
    const auto size = reader.u64();
    std::vector<MeasurementIndexEntry> entries;
    entries.reserve(static_cast<std::size_t>(size));
    for (std::uint64_t index = 0; index < size; ++index) {
        MeasurementIndexEntry entry;
        entry.branch = reader.string();
        entry.commit = read_commit_id(reader);
        entry.spec_hash = reader.hash();
        entry.measurement_object = reader.hash();
        entry.measurement_identity_hash = entry.measurement_object;
        entry.risk.structural = reader.u64();
        entry.risk.law_distance = reader.u64();
        entry.risk.repair_distance = reader.u64();
        entry.risk.surprise = reader.u64();
        entry.projection = reader.u64();
        entry.needs_rebuild = true;
        entries.push_back(std::move(entry));
    }
    reader.expect_end();
    return entries;
}

std::vector<MeasurementIndexEntry> read_entries_v2(const std::vector<std::byte>& payload) {
    IndexPayloadReader reader{payload};
    if (reader.string() != "MeasurementIndex:v2") {
        throw std::runtime_error("measurement index payload has unsupported version");
    }
    const auto size = reader.u64();
    std::vector<MeasurementIndexEntry> entries;
    entries.reserve(static_cast<std::size_t>(size));
    for (std::uint64_t index = 0; index < size; ++index) {
        MeasurementIndexEntry entry;
        entry.branch = reader.string();
        entry.commit = read_commit_id(reader);
        entry.spec_hash = reader.hash();
        entry.measurement_object = reader.hash();
        entry.measurement_identity_hash = reader.hash();
        entry.component_root = reader.hash();
        entry.evidence_root = reader.hash();
        entry.risk.structural = reader.u64();
        entry.risk.law_distance = reader.u64();
        entry.risk.repair_distance = reader.u64();
        entry.risk.surprise = reader.u64();
        entry.projection = reader.u64();
        entry.needs_rebuild = reader.boolean();
        entries.push_back(std::move(entry));
    }
    reader.expect_end();
    return entries;
}

}  // namespace

MeasurementIndex::MeasurementIndex(std::filesystem::path root)
    : store_(std::move(root), "measurements.idx", "PVMEASUREIDX2") {}

bool MeasurementIndex::exists() const {
    return store_.exists();
}

std::vector<MeasurementIndexEntry> MeasurementIndex::entries() const {
    if (!exists()) {
        return {};
    }
    try {
        const auto payload = store_.read_payload();
        return read_entries_v2(payload);
    } catch (const std::exception&) {
        IndexStore legacy{store_.root(), "measurements.idx", "PVMEASUREIDX1"};
        if (!legacy.exists()) {
            throw;
        }
        return read_entries_v1(legacy.read_payload());
    }
}

std::optional<MeasurementIndexEntry> MeasurementIndex::find(CommitId commit, Hash256 spec) const {
    for (const auto& entry : entries()) {
        if (entry.commit == commit && entry.spec_hash == spec) {
            return entry;
        }
    }
    return std::nullopt;
}

std::optional<MeasurementIndexEntry> MeasurementIndex::find(
    std::string_view branch,
    CommitId commit,
    Hash256 spec) const {
    for (const auto& entry : entries()) {
        if (entry.branch == branch && entry.commit == commit && entry.spec_hash == spec) {
            return entry;
        }
    }
    return std::nullopt;
}

std::vector<MeasurementIndexEntry> MeasurementIndex::branch_entries(
    std::string_view branch,
    Hash256 spec) const {
    std::vector<MeasurementIndexEntry> out;
    for (const auto& entry : entries()) {
        if (entry.branch == branch && entry.spec_hash == spec) {
            out.push_back(entry);
        }
    }
    std::ranges::sort(out, [](const auto& left, const auto& right) {
        return to_hex(left.commit.value) < to_hex(right.commit.value);
    });
    return out;
}

IndexFileStatus MeasurementIndex::check() const {
    auto status = store_.check();
    if (!status.exists || status.checksum_ok) {
        return status;
    }
    IndexStore legacy{store_.root(), "measurements.idx", "PVMEASUREIDX1"};
    auto legacy_status = legacy.check();
    if (legacy_status.checksum_ok) {
        legacy_status.error = "legacy measurement index needs rebuild";
    }
    return legacy_status;
}

Hash256 MeasurementIndex::checksum() const {
    try {
        return store_.checksum();
    } catch (const std::exception&) {
        return IndexStore{store_.root(), "measurements.idx", "PVMEASUREIDX1"}.checksum();
    }
}

void MeasurementIndex::write(std::vector<MeasurementIndexEntry> entries) const {
    sort_entries(entries);
    entries.erase(std::unique(entries.begin(), entries.end(), same_key), entries.end());

    IndexPayloadWriter writer;
    writer.string("MeasurementIndex:v2");
    writer.u64(entries.size());
    for (const auto& entry : entries) {
        writer.string(entry.branch);
        write_commit_id(writer, entry.commit);
        writer.hash(entry.spec_hash);
        writer.hash(entry.measurement_object);
        writer.hash(entry.measurement_identity_hash);
        writer.hash(entry.component_root);
        writer.hash(entry.evidence_root);
        writer.u64(entry.risk.structural);
        writer.u64(entry.risk.law_distance);
        writer.u64(entry.risk.repair_distance);
        writer.u64(entry.risk.surprise);
        writer.u64(entry.projection);
        writer.boolean(entry.needs_rebuild);
    }
    store_.write_payload(writer.bytes());
}

void MeasurementIndex::upsert(std::string_view branch, MeasurementIndexEntry entry) const {
    entry.branch = std::string{branch};
    auto all = entries();
    auto iter = std::ranges::find_if(all, [&](const auto& item) {
        return same_key(item, entry);
    });
    if (iter == all.end()) {
        all.push_back(entry);
    } else {
        *iter = entry;
    }
    write(std::move(all));
}

void MeasurementIndex::remove_branch_spec(std::string_view branch, Hash256 spec) const {
    auto all = entries();
    all.erase(std::remove_if(all.begin(), all.end(), [&](const auto& entry) {
        return entry.branch == branch && entry.spec_hash == spec;
    }), all.end());
    write(std::move(all));
}

void MeasurementIndex::remove() const {
    store_.remove();
}

}  // namespace pv
