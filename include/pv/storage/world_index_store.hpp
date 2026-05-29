// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pv/core/id.hpp"
#include "pv/core/pointer.hpp"
#include "pv/core/snapshot.hpp"
#include "pv/storage/index_store.hpp"

namespace pv {

struct WorldObjectNameIndexEntry {
    std::string name;
    ObjectId object;
};

struct WorldTypeIndexEntry {
    std::string type;
    std::vector<ObjectId> objects;
};

struct WorldRelationIndexEntry {
    std::string relation;
    std::vector<PointerId> pointers;
};

struct WorldIndexBranchEntry {
    std::string branch;
    Hash256 snapshot;
    std::vector<WorldObjectNameIndexEntry> names;
    std::vector<WorldTypeIndexEntry> types;
    std::vector<WorldRelationIndexEntry> relations;
};

struct WorldIndexStats {
    std::size_t branches{0};
    std::size_t object_names{0};
    std::size_t type_entries{0};
    std::size_t relation_entries{0};
};

class WorldIndexStore {
public:
    explicit WorldIndexStore(std::filesystem::path root);

    [[nodiscard]] bool exists() const;
    [[nodiscard]] std::vector<WorldIndexBranchEntry> entries() const;
    [[nodiscard]] std::optional<WorldIndexBranchEntry> find_branch(std::string_view branch) const;
    [[nodiscard]] std::vector<ObjectId> objects_by_type(std::string_view branch, std::string_view type) const;
    [[nodiscard]] std::optional<ObjectId> object_by_name(std::string_view branch, std::string_view name) const;
    [[nodiscard]] std::vector<PointerId> links_by_relation(std::string_view branch, std::string_view relation) const;
    [[nodiscard]] WorldIndexStats stats() const;
    [[nodiscard]] IndexFileStatus check() const;
    [[nodiscard]] IndexFileStatus relations_check() const;
    [[nodiscard]] Hash256 checksum() const;
    [[nodiscard]] Hash256 relations_checksum() const;

    void write(std::vector<WorldIndexBranchEntry> entries) const;
    void update_branch(std::string branch, Hash256 snapshot, const WorldSnapshot& world) const;
    void remove() const;

private:
    IndexStore object_store_;
    IndexStore relation_store_;
};

}  // namespace pv
