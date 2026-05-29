// SPDX-License-Identifier: Apache-2.0
#include "pv/measure/measurement_index.hpp"

#include <algorithm>
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

}  // namespace

MeasurementIndex::MeasurementIndex(std::filesystem::path root)
    : store_(std::move(root), "measurements.idx", "PVMEASUREIDX1") {}

bool MeasurementIndex::exists() const {
    return store_.exists();
}

std::vector<MeasurementIndexEntry> MeasurementIndex::entries() const {
    if (!exists()) {
        return {};
    }
    const auto payload = store_.read_payload();
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
        entry.risk.structural = reader.u64();
        entry.risk.law_distance = reader.u64();
        entry.risk.repair_distance = reader.u64();
        entry.risk.surprise = reader.u64();
        entry.projection = reader.u64();
        entries.push_back(std::move(entry));
    }
    reader.expect_end();
    return entries;
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
    return store_.check();
}

Hash256 MeasurementIndex::checksum() const {
    return store_.checksum();
}

void MeasurementIndex::write(std::vector<MeasurementIndexEntry> entries) const {
    sort_entries(entries);
    entries.erase(std::unique(entries.begin(), entries.end(), same_key), entries.end());

    IndexPayloadWriter writer;
    writer.u64(entries.size());
    for (const auto& entry : entries) {
        writer.string(entry.branch);
        write_commit_id(writer, entry.commit);
        writer.hash(entry.spec_hash);
        writer.hash(entry.measurement_object);
        writer.u64(entry.risk.structural);
        writer.u64(entry.risk.law_distance);
        writer.u64(entry.risk.repair_distance);
        writer.u64(entry.risk.surprise);
        writer.u64(entry.projection);
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
