// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

#include "pv/hash/canonical.hpp"

namespace pv {

struct PackReport {
    std::size_t packed_objects{0};
    std::size_t packed_bytes{0};
    std::filesystem::path pack_path;
    std::filesystem::path index_path;
};

class PackedContentStore {
public:
    explicit PackedContentStore(std::filesystem::path root);

    [[nodiscard]] bool contains(Hash256 id) const;
    [[nodiscard]] std::vector<std::byte> get_bytes(Hash256 id) const;
    [[nodiscard]] std::size_t packed_object_count() const;
    [[nodiscard]] PackReport compact_loose_objects();

private:
    struct PackIndexEntry {
        Hash256 id;
        std::filesystem::path pack_path;
        std::uint64_t offset{0};
        std::uint64_t size{0};
    };

    [[nodiscard]] std::vector<PackIndexEntry> index_entries() const;
    [[nodiscard]] std::optional<PackIndexEntry> find(Hash256 id) const;
    [[nodiscard]] std::filesystem::path packs_root() const;
    [[nodiscard]] std::uint64_t next_pack_number() const;

    std::filesystem::path root_;
};

}  // namespace pv
