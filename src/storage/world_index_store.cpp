// SPDX-License-Identifier: Apache-2.0
#include "pv/storage/world_index_store.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
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

void write_commit(IndexPayloadWriter& writer, CommitId id) {
    writer.hash(id.value);
}

CommitId read_commit(IndexPayloadReader& reader) {
    return CommitId{reader.hash()};
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

void sort_pointers(std::vector<PointerId>& pointers) {
    std::ranges::sort(pointers, {}, &PointerId::value);
    pointers.erase(std::ranges::unique(pointers).begin(), pointers.end());
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

bool active_at(const PointerSnapshot& pointer, Epoch epoch) noexcept {
    return pointer.born_at <= epoch && (!pointer.expires_at.has_value() || epoch < *pointer.expires_at);
}

std::uint64_t weight_scaled(Weight weight) noexcept {
    if (!std::isfinite(weight.value) || weight.value <= 0.0) {
        return 0;
    }
    constexpr double scale = 1000000.0;
    if (weight.value >= static_cast<double>(std::numeric_limits<std::uint64_t>::max()) / scale) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return static_cast<std::uint64_t>(std::llround(weight.value * scale));
}

void sort_adjacency(WorldGraphAdjacencyEntry& entry) {
    sort_pointers(entry.outgoing);
    sort_pointers(entry.incoming);
}

void sort_graph_entry(WorldGraphIndexEntry& entry) {
    std::ranges::sort(entry.names, {}, &WorldObjectNameIndexEntry::name);
    std::ranges::sort(entry.types, {}, &WorldTypeIndexEntry::type);
    std::ranges::sort(entry.relations, {}, &WorldRelationIndexEntry::relation);
    std::ranges::sort(entry.pointers, {}, [](const WorldGraphPointerEntry& pointer) {
        return pointer.pointer.value;
    });
    for (auto& adjacency : entry.adjacency) {
        sort_adjacency(adjacency);
    }
    std::ranges::sort(entry.adjacency, [](const WorldGraphAdjacencyEntry& left, const WorldGraphAdjacencyEntry& right) {
        if (left.object.index != right.object.index) {
            return left.object.index < right.object.index;
        }
        return left.object.generation < right.object.generation;
    });
}

std::string commit_key(CommitId id) {
    return to_hex(id.value);
}

void sort_graph_entries(std::vector<WorldGraphIndexEntry>& entries) {
    for (auto& entry : entries) {
        sort_graph_entry(entry);
    }
    std::ranges::sort(entries, [](const WorldGraphIndexEntry& left, const WorldGraphIndexEntry& right) {
        return commit_key(left.commit) < commit_key(right.commit);
    });
}

}  // namespace

WorldIndexStore::WorldIndexStore(std::filesystem::path root)
    : object_store_(root, "objects.idx", "PVWORLDIDX1"),
      relation_store_(root, "relations.idx", "PVRELIDX1"),
      graph_store_(std::move(root), "graph.idx", "PVGRAPHIDX1") {}

bool WorldIndexStore::exists() const {
    return object_store_.exists() && relation_store_.exists() && graph_store_.exists();
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

std::vector<WorldGraphIndexEntry> WorldIndexStore::commit_entries() const {
    if (!graph_store_.exists()) {
        return {};
    }
    const auto payload = graph_store_.read_payload();
    IndexPayloadReader reader{payload};
    const auto commit_count = reader.u64();
    std::vector<WorldGraphIndexEntry> entries;
    entries.reserve(static_cast<std::size_t>(commit_count));
    for (std::uint64_t commit_index = 0; commit_index < commit_count; ++commit_index) {
        WorldGraphIndexEntry entry;
        entry.commit = read_commit(reader);
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

        const auto pointers = reader.u64();
        entry.pointers.reserve(static_cast<std::size_t>(pointers));
        for (std::uint64_t index = 0; index < pointers; ++index) {
            WorldGraphPointerEntry pointer;
            pointer.pointer = read_pointer(reader);
            pointer.from = read_object(reader);
            pointer.to = read_object(reader);
            pointer.relation = reader.string();
            pointer.weight_scaled = reader.u64();
            entry.pointers.push_back(std::move(pointer));
        }

        const auto adjacency = reader.u64();
        entry.adjacency.reserve(static_cast<std::size_t>(adjacency));
        for (std::uint64_t index = 0; index < adjacency; ++index) {
            entry.adjacency.push_back(WorldGraphAdjacencyEntry{
                read_object(reader),
                read_pointers(reader),
                read_pointers(reader)
            });
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

std::optional<WorldGraphIndexEntry> WorldIndexStore::find_commit(CommitId commit) const {
    for (const auto& entry : commit_entries()) {
        if (entry.commit == commit) {
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

IndexFileStatus WorldIndexStore::graph_check() const {
    return graph_store_.check();
}

Hash256 WorldIndexStore::checksum() const {
    return object_store_.checksum();
}

Hash256 WorldIndexStore::relations_checksum() const {
    return relation_store_.checksum();
}

Hash256 WorldIndexStore::graph_checksum() const {
    return graph_store_.checksum();
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

void WorldIndexStore::index_commit(CommitId commit, Hash256 snapshot, const WorldSnapshot& world) const {
    WorldIndex runtime_index;
    runtime_index.rebuild(world);

    WorldGraphIndexEntry next;
    next.commit = commit;
    next.snapshot = snapshot;
    next.names.reserve(world.objects.size());
    next.adjacency.reserve(world.objects.size());
    for (const auto& object : world.objects) {
        next.names.push_back(WorldObjectNameIndexEntry{object.name, object.id});
        next.adjacency.push_back(WorldGraphAdjacencyEntry{
            object.id,
            {runtime_index.outgoing(object.id).begin(), runtime_index.outgoing(object.id).end()},
            {runtime_index.incoming(object.id).begin(), runtime_index.incoming(object.id).end()}
        });
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
    for (const auto& pointer : world.pointers) {
        if (!active_at(pointer, world.epoch)) {
            continue;
        }
        next.pointers.push_back(WorldGraphPointerEntry{
            pointer.id,
            pointer.from,
            pointer.to,
            world.relation_name(pointer.relation),
            weight_scaled(pointer.weight)
        });
    }

    auto all = commit_entries();
    auto iter = std::ranges::find(all, next.commit, &WorldGraphIndexEntry::commit);
    if (iter == all.end()) {
        all.push_back(std::move(next));
    } else {
        *iter = std::move(next);
    }
    sort_graph_entries(all);
    all.erase(std::ranges::unique(all, {}, &WorldGraphIndexEntry::commit).begin(), all.end());

    IndexPayloadWriter writer;
    writer.u64(all.size());
    for (const auto& entry : all) {
        write_commit(writer, entry.commit);
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

        writer.u64(entry.pointers.size());
        for (const auto& pointer : entry.pointers) {
            write_pointer(writer, pointer.pointer);
            write_object(writer, pointer.from);
            write_object(writer, pointer.to);
            writer.string(pointer.relation);
            writer.u64(pointer.weight_scaled);
        }

        writer.u64(entry.adjacency.size());
        for (const auto& adjacency : entry.adjacency) {
            write_object(writer, adjacency.object);
            write_pointers(writer, adjacency.outgoing);
            write_pointers(writer, adjacency.incoming);
        }
    }
    graph_store_.write_payload(writer.bytes());
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
    graph_store_.remove();
}

}  // namespace pv
