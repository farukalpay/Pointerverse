// SPDX-License-Identifier: Apache-2.0
#include "pv/storage/world_index_store.hpp"

#include <algorithm>
#include <utility>

#include "pv/core/world_index.hpp"
#include "pv/hash/canonical.hpp"

namespace pv {
namespace {

void write_object(IndexPayloadWriter& writer, ObjectId id) {
    writer.u32(id.index);
    writer.u32(id.generation);
}

ObjectId read_object(IndexPayloadReader& reader) {
    return ObjectId{reader.u32(), reader.u32()};
}

void write_pointer(IndexPayloadWriter& writer, PointerId id) {
    writer.u64(id.value);
}

PointerId read_pointer(IndexPayloadReader& reader) {
    return PointerId{reader.u64()};
}

void write_objects(IndexPayloadWriter& writer, const std::vector<ObjectId>& objects) {
    writer.u64(objects.size());
    for (const auto object : objects) {
        write_object(writer, object);
    }
}

std::vector<ObjectId> read_objects(IndexPayloadReader& reader) {
    const auto size = reader.u64();
    std::vector<ObjectId> objects;
    objects.reserve(static_cast<std::size_t>(size));
    for (std::uint64_t index = 0; index < size; ++index) {
        objects.push_back(read_object(reader));
    }
    return objects;
}

void write_pointers(IndexPayloadWriter& writer, const std::vector<PointerId>& pointers) {
    writer.u64(pointers.size());
    for (const auto pointer : pointers) {
        write_pointer(writer, pointer);
    }
}

std::vector<PointerId> read_pointers(IndexPayloadReader& reader) {
    const auto size = reader.u64();
    std::vector<PointerId> pointers;
    pointers.reserve(static_cast<std::size_t>(size));
    for (std::uint64_t index = 0; index < size; ++index) {
        pointers.push_back(read_pointer(reader));
    }
    return pointers;
}

void sort_entry(WorldIndexBranchEntry& entry) {
    std::ranges::sort(entry.names, {}, &WorldObjectNameIndexEntry::name);
    std::ranges::sort(entry.types, {}, &WorldTypeIndexEntry::type);
    std::ranges::sort(entry.relations, {}, &WorldRelationIndexEntry::relation);
}

void sort_entries(std::vector<WorldIndexBranchEntry>& entries) {
    for (auto& entry : entries) {
        sort_entry(entry);
    }
    std::ranges::sort(entries, {}, &WorldIndexBranchEntry::branch);
}

}  // namespace

WorldIndexStore::WorldIndexStore(std::filesystem::path root)
    : object_store_(root, "objects.idx", "PVWORLDIDX1"),
      relation_store_(std::move(root), "relations.idx", "PVRELIDX1") {}

bool WorldIndexStore::exists() const {
    return object_store_.exists() && relation_store_.exists();
}

std::vector<WorldIndexBranchEntry> WorldIndexStore::entries() const {
    if (!object_store_.exists()) {
        return {};
    }
    const auto payload = object_store_.read_payload();
    IndexPayloadReader reader{payload};
    const auto branch_count = reader.u64();
    std::vector<WorldIndexBranchEntry> entries;
    entries.reserve(static_cast<std::size_t>(branch_count));
    for (std::uint64_t branch_index = 0; branch_index < branch_count; ++branch_index) {
        WorldIndexBranchEntry entry;
        entry.branch = reader.string();
        entry.snapshot = reader.hash();

        const auto names = reader.u64();
        entry.names.reserve(static_cast<std::size_t>(names));
        for (std::uint64_t index = 0; index < names; ++index) {
            entry.names.push_back(WorldObjectNameIndexEntry{reader.string(), read_object(reader)});
        }

        const auto types = reader.u64();
        entry.types.reserve(static_cast<std::size_t>(types));
        for (std::uint64_t index = 0; index < types; ++index) {
            entry.types.push_back(WorldTypeIndexEntry{reader.string(), read_objects(reader)});
        }

        const auto relations = reader.u64();
        entry.relations.reserve(static_cast<std::size_t>(relations));
        for (std::uint64_t index = 0; index < relations; ++index) {
            entry.relations.push_back(WorldRelationIndexEntry{reader.string(), read_pointers(reader)});
        }
        entries.push_back(std::move(entry));
    }
    reader.expect_end();
    return entries;
}

std::optional<WorldIndexBranchEntry> WorldIndexStore::find_branch(std::string_view branch) const {
    for (const auto& entry : entries()) {
        if (entry.branch == branch) {
            return entry;
        }
    }
    return std::nullopt;
}

std::vector<ObjectId> WorldIndexStore::objects_by_type(std::string_view branch, std::string_view type) const {
    const auto entry = find_branch(branch);
    if (!entry.has_value()) {
        return {};
    }
    for (const auto& bucket : entry->types) {
        if (bucket.type == type) {
            return bucket.objects;
        }
    }
    return {};
}

std::optional<ObjectId> WorldIndexStore::object_by_name(std::string_view branch, std::string_view name) const {
    const auto entry = find_branch(branch);
    if (!entry.has_value()) {
        return std::nullopt;
    }
    for (const auto& candidate : entry->names) {
        if (candidate.name == name) {
            return candidate.object;
        }
    }
    return std::nullopt;
}

std::vector<PointerId> WorldIndexStore::links_by_relation(std::string_view branch, std::string_view relation) const {
    const auto entry = find_branch(branch);
    if (!entry.has_value()) {
        return {};
    }
    for (const auto& bucket : entry->relations) {
        if (bucket.relation == relation) {
            return bucket.pointers;
        }
    }
    return {};
}

WorldIndexStats WorldIndexStore::stats() const {
    WorldIndexStats stats;
    for (const auto& entry : entries()) {
        stats.branches += 1;
        stats.object_names += entry.names.size();
        stats.type_entries += entry.types.size();
        stats.relation_entries += entry.relations.size();
    }
    return stats;
}

IndexFileStatus WorldIndexStore::check() const {
    return object_store_.check();
}

IndexFileStatus WorldIndexStore::relations_check() const {
    return relation_store_.check();
}

Hash256 WorldIndexStore::checksum() const {
    return object_store_.checksum();
}

Hash256 WorldIndexStore::relations_checksum() const {
    return relation_store_.checksum();
}

void WorldIndexStore::write(std::vector<WorldIndexBranchEntry> entries) const {
    sort_entries(entries);
    entries.erase(std::ranges::unique(entries, {}, &WorldIndexBranchEntry::branch).begin(), entries.end());
    IndexPayloadWriter writer;
    writer.u64(entries.size());
    for (const auto& entry : entries) {
        writer.string(entry.branch);
        writer.hash(entry.snapshot);

        writer.u64(entry.names.size());
        for (const auto& name : entry.names) {
            writer.string(name.name);
            write_object(writer, name.object);
        }

        writer.u64(entry.types.size());
        for (const auto& type : entry.types) {
            writer.string(type.type);
            write_objects(writer, type.objects);
        }

        writer.u64(entry.relations.size());
        for (const auto& relation : entry.relations) {
            writer.string(relation.relation);
            write_pointers(writer, relation.pointers);
        }
    }
    object_store_.write_payload(writer.bytes());
    relation_store_.write_payload(writer.bytes());
}

void WorldIndexStore::update_branch(std::string branch, Hash256 snapshot, const WorldSnapshot& world) const {
    WorldIndex runtime_index;
    runtime_index.rebuild(world);

    WorldIndexBranchEntry next;
    next.branch = std::move(branch);
    next.snapshot = snapshot;
    next.names.reserve(world.objects.size());
    for (const auto& object : world.objects) {
        next.names.push_back(WorldObjectNameIndexEntry{object.name, object.id});
    }

    for (const auto& [id, name] : world.type_names) {
        const auto span = runtime_index.objects_by_type(TypeId{id});
        if (!span.empty()) {
            next.types.push_back(WorldTypeIndexEntry{name, {span.begin(), span.end()}});
        }
    }
    for (const auto& [id, name] : world.relation_names) {
        const auto span = runtime_index.relation(RelationType{id});
        if (!span.empty()) {
            next.relations.push_back(WorldRelationIndexEntry{name, {span.begin(), span.end()}});
        }
    }

    auto all = entries();
    auto iter = std::ranges::find(all, next.branch, &WorldIndexBranchEntry::branch);
    if (iter == all.end()) {
        all.push_back(std::move(next));
    } else {
        *iter = std::move(next);
    }
    write(std::move(all));
}

void WorldIndexStore::remove() const {
    object_store_.remove();
    relation_store_.remove();
}

}  // namespace pv
