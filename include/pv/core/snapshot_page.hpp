// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "pv/core/fact.hpp"
#include "pv/core/snapshot.hpp"
#include "pv/hash/canonical.hpp"

namespace pv {

inline constexpr std::size_t default_snapshot_page_size = 256;

struct ObjectPage {
    std::vector<ObjectSnapshot> objects;
};

struct PointerPage {
    std::vector<PointerSnapshot> pointers;
};

struct FactPage {
    std::vector<Fact> facts;
};

struct SymbolTableObject {
    std::map<std::uint32_t, std::string> names;
};

struct SnapshotPageIndexObject {
    std::vector<Hash256> pages;
};

struct SnapshotRootObject {
    WorldId world;
    std::string world_name;
    Epoch epoch;
    Hash256 object_pages_root;
    Hash256 pointer_pages_root;
    Hash256 fact_pages_root;
    Hash256 type_table_root;
    Hash256 relation_table_root;
};

struct ChunkedSnapshotPlan {
    SnapshotRootObject root;
    Hash256 root_object;
    std::vector<ObjectPage> object_pages;
    std::vector<Hash256> object_page_hashes;
    std::vector<PointerPage> pointer_pages;
    std::vector<Hash256> pointer_page_hashes;
    std::vector<FactPage> fact_pages;
    std::vector<Hash256> fact_page_hashes;
    SymbolTableObject type_table;
    Hash256 type_table_hash;
    SymbolTableObject relation_table;
    Hash256 relation_table_hash;
    SnapshotPageIndexObject object_page_index;
    Hash256 object_page_index_hash;
    SnapshotPageIndexObject pointer_page_index;
    Hash256 pointer_page_index_hash;
    SnapshotPageIndexObject fact_page_index;
    Hash256 fact_page_index_hash;

    [[nodiscard]] std::vector<Hash256> graph_page_roots() const;
};

[[nodiscard]] ChunkedSnapshotPlan build_chunked_snapshot_plan(
    const WorldSnapshot& snapshot,
    std::size_t page_size = default_snapshot_page_size);

}  // namespace pv
