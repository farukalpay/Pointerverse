// SPDX-License-Identifier: Apache-2.0
#include "pv/storage/chunked_snapshot_store.hpp"

#include <stdexcept>
#include <utility>

#include "pv/hash/hasher.hpp"

namespace pv {
namespace {

template <class T>
Hash256 put_checked(ContentStore& objects, const T& value, Hash256 expected) {
    const auto actual = objects.put_canonical(value);
    if (actual != expected) {
        throw std::runtime_error("chunked snapshot object hash mismatch");
    }
    return actual;
}

template <class Page>
void append_pages(ContentStore& objects, const std::vector<Hash256>& hashes, std::vector<Page>& pages) {
    for (const auto hash : hashes) {
        pages.push_back(objects.get_canonical<Page>(hash));
    }
}

}  // namespace

ChunkedSnapshotStore::ChunkedSnapshotStore(ContentStore& objects, std::size_t page_size)
    : objects_(objects), page_size_(page_size) {}

ChunkedSnapshotPlan ChunkedSnapshotStore::plan(const WorldSnapshot& snapshot) const {
    return build_chunked_snapshot_plan(snapshot, page_size_);
}

Hash256 ChunkedSnapshotStore::put_snapshot(const WorldSnapshot& snapshot) const {
    auto chunked = plan(snapshot);
    for (std::size_t index = 0; index < chunked.object_pages.size(); ++index) {
        put_checked(objects_, chunked.object_pages[index], chunked.object_page_hashes[index]);
    }
    for (std::size_t index = 0; index < chunked.pointer_pages.size(); ++index) {
        put_checked(objects_, chunked.pointer_pages[index], chunked.pointer_page_hashes[index]);
    }
    for (std::size_t index = 0; index < chunked.fact_pages.size(); ++index) {
        put_checked(objects_, chunked.fact_pages[index], chunked.fact_page_hashes[index]);
    }
    put_checked(objects_, chunked.object_page_index, chunked.object_page_index_hash);
    put_checked(objects_, chunked.pointer_page_index, chunked.pointer_page_index_hash);
    put_checked(objects_, chunked.fact_page_index, chunked.fact_page_index_hash);
    put_checked(objects_, chunked.type_table, chunked.type_table_hash);
    put_checked(objects_, chunked.relation_table, chunked.relation_table_hash);
    return put_checked(objects_, chunked.root, chunked.root_object);
}

WorldSnapshot ChunkedSnapshotStore::get_snapshot(Hash256 root_object) const {
    const auto root = objects_.get_canonical<SnapshotRootObject>(root_object);
    const auto object_index = objects_.get_canonical<SnapshotPageIndexObject>(root.object_pages_root);
    const auto pointer_index = objects_.get_canonical<SnapshotPageIndexObject>(root.pointer_pages_root);
    const auto fact_index = objects_.get_canonical<SnapshotPageIndexObject>(root.fact_pages_root);
    const auto type_table = objects_.get_canonical<SymbolTableObject>(root.type_table_root);
    const auto relation_table = objects_.get_canonical<SymbolTableObject>(root.relation_table_root);

    WorldSnapshot snapshot;
    snapshot.world = root.world;
    snapshot.world_name = root.world_name;
    snapshot.epoch = root.epoch;
    snapshot.type_names = type_table.names;
    snapshot.relation_names = relation_table.names;

    std::vector<ObjectPage> object_pages;
    append_pages(objects_, object_index.pages, object_pages);
    for (auto& page : object_pages) {
        snapshot.objects.insert(
            snapshot.objects.end(),
            std::make_move_iterator(page.objects.begin()),
            std::make_move_iterator(page.objects.end()));
    }

    std::vector<PointerPage> pointer_pages;
    append_pages(objects_, pointer_index.pages, pointer_pages);
    for (auto& page : pointer_pages) {
        snapshot.pointers.insert(
            snapshot.pointers.end(),
            std::make_move_iterator(page.pointers.begin()),
            std::make_move_iterator(page.pointers.end()));
    }

    std::vector<FactPage> fact_pages;
    append_pages(objects_, fact_index.pages, fact_pages);
    for (auto& page : fact_pages) {
        snapshot.facts.insert(
            snapshot.facts.end(),
            std::make_move_iterator(page.facts.begin()),
            std::make_move_iterator(page.facts.end()));
    }

    const auto rebuilt = build_chunked_snapshot_plan(snapshot, page_size_);
    if (rebuilt.root_object != root_object) {
        throw std::runtime_error("chunked snapshot root mismatch");
    }
    return snapshot;
}

}  // namespace pv
