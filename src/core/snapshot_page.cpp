// SPDX-License-Identifier: Apache-2.0
#include "pv/core/snapshot_page.hpp"

#include <algorithm>
#include <stdexcept>

#include "pv/hash/hasher.hpp"
#include "pv/kernel/canonical_codec.hpp"

namespace pv {
namespace {

template <class T>
Hash256 hash_canonical(const T& value) {
    return sha256(canonical_encode(value));
}

template <class T, class Page, class Assign>
void build_pages(
    const std::vector<T>& values,
    std::size_t page_size,
    std::vector<Page>& pages,
    std::vector<Hash256>& page_hashes,
    Assign assign) {
    for (std::size_t offset = 0; offset < values.size(); offset += page_size) {
        Page page;
        const auto end = std::min(values.size(), offset + page_size);
        assign(page, values.begin() + static_cast<std::ptrdiff_t>(offset), values.begin() + static_cast<std::ptrdiff_t>(end));
        page_hashes.push_back(hash_canonical(page));
        pages.push_back(std::move(page));
    }
}

}  // namespace

std::vector<Hash256> ChunkedSnapshotPlan::graph_page_roots() const {
    std::vector<Hash256> roots;
    roots.reserve(
        object_page_hashes.size()
        + pointer_page_hashes.size()
        + fact_page_hashes.size()
        + 5);
    roots.insert(roots.end(), object_page_hashes.begin(), object_page_hashes.end());
    roots.insert(roots.end(), pointer_page_hashes.begin(), pointer_page_hashes.end());
    roots.insert(roots.end(), fact_page_hashes.begin(), fact_page_hashes.end());
    roots.push_back(object_page_index_hash);
    roots.push_back(pointer_page_index_hash);
    roots.push_back(fact_page_index_hash);
    roots.push_back(type_table_hash);
    roots.push_back(relation_table_hash);
    return roots;
}

ChunkedSnapshotPlan build_chunked_snapshot_plan(const WorldSnapshot& snapshot, std::size_t page_size) {
    if (page_size == 0) {
        throw std::invalid_argument("snapshot page size must be positive");
    }

    ChunkedSnapshotPlan plan;
    build_pages<ObjectSnapshot, ObjectPage>(
        snapshot.objects,
        page_size,
        plan.object_pages,
        plan.object_page_hashes,
        [](ObjectPage& page, auto begin, auto end) {
            page.objects.assign(begin, end);
        });
    build_pages<PointerSnapshot, PointerPage>(
        snapshot.pointers,
        page_size,
        plan.pointer_pages,
        plan.pointer_page_hashes,
        [](PointerPage& page, auto begin, auto end) {
            page.pointers.assign(begin, end);
        });
    build_pages<Fact, FactPage>(
        snapshot.facts,
        page_size,
        plan.fact_pages,
        plan.fact_page_hashes,
        [](FactPage& page, auto begin, auto end) {
            page.facts.assign(begin, end);
        });

    plan.object_page_index.pages = plan.object_page_hashes;
    plan.object_page_index_hash = hash_canonical(plan.object_page_index);
    plan.pointer_page_index.pages = plan.pointer_page_hashes;
    plan.pointer_page_index_hash = hash_canonical(plan.pointer_page_index);
    plan.fact_page_index.pages = plan.fact_page_hashes;
    plan.fact_page_index_hash = hash_canonical(plan.fact_page_index);

    plan.type_table.names = snapshot.type_names;
    plan.type_table_hash = hash_canonical(plan.type_table);
    plan.relation_table.names = snapshot.relation_names;
    plan.relation_table_hash = hash_canonical(plan.relation_table);

    plan.root.world = snapshot.world;
    plan.root.world_name = snapshot.world_name;
    plan.root.epoch = snapshot.epoch;
    plan.root.object_pages_root = plan.object_page_index_hash;
    plan.root.pointer_pages_root = plan.pointer_page_index_hash;
    plan.root.fact_pages_root = plan.fact_page_index_hash;
    plan.root.type_table_root = plan.type_table_hash;
    plan.root.relation_table_root = plan.relation_table_hash;
    plan.root_object = hash_canonical(plan.root);
    return plan;
}

}  // namespace pv
