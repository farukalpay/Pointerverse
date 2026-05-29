// SPDX-License-Identifier: Apache-2.0
#include "pv/storage/content_store.hpp"

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>

#include "pv/hash/hasher.hpp"
#include "pv/storage/pack_store.hpp"

namespace pv {
namespace {

std::vector<std::byte> read_binary_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open object '" + path.string() + "'");
    }

    std::vector<std::byte> out;
    for (std::istreambuf_iterator<char> iter{input}, end; iter != end; ++iter) {
        out.push_back(static_cast<std::byte>(static_cast<unsigned char>(*iter)));
    }
    return out;
}

void write_binary_file(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot write object '" + path.string() + "'");
    }
    for (const auto byte : bytes) {
        output.put(static_cast<char>(byte));
    }
    output.flush();
    if (!output) {
        throw std::runtime_error("failed writing object '" + path.string() + "'");
    }
}

template <class T, class Decode>
T decode_with(std::span<const std::byte> bytes, Decode decode) {
    CanonicalReader reader{bytes};
    auto out = decode(reader);
    reader.expect_end();
    return out;
}

}  // namespace

ContentStore::ContentStore(std::filesystem::path root) : root_(std::move(root)) {
    std::filesystem::create_directories(root_ / "objects");
}

const std::filesystem::path& ContentStore::root() const noexcept {
    return root_;
}

std::filesystem::path ContentStore::object_path(Hash256 id) const {
    const auto hex = to_hex(id);
    return root_ / "objects" / hex.substr(0, 2) / hex;
}

Hash256 ContentStore::put_bytes(std::span<const std::byte> bytes) {
    const auto id = sha256(bytes);
    const auto path = object_path(id);
    if (std::filesystem::exists(path)) {
        (void)get_bytes(id);
        return id;
    }

    std::filesystem::create_directories(path.parent_path());
    const auto tmp = path.string() + ".tmp";
    write_binary_file(tmp, bytes);
    std::filesystem::rename(tmp, path);
    return id;
}

std::vector<std::byte> ContentStore::get_bytes(Hash256 id) const {
    const auto path = object_path(id);
    if (!std::filesystem::exists(path)) {
        return PackedContentStore{root_}.get_bytes(id);
    }
    auto bytes = read_binary_file(path);
    const auto actual = sha256(bytes);
    if (actual != id) {
        throw std::runtime_error("object hash mismatch for '" + path.string() + "'");
    }
    return bytes;
}

bool ContentStore::contains(Hash256 id) const {
    return std::filesystem::exists(object_path(id)) || PackedContentStore{root_}.contains(id);
}

template <>
WorldSnapshot decode_canonical<WorldSnapshot>(std::span<const std::byte> bytes) {
    return decode_with<WorldSnapshot>(bytes, decode_world_snapshot);
}

template <>
Delta decode_canonical<Delta>(std::span<const std::byte> bytes) {
    return decode_with<Delta>(bytes, decode_delta);
}

template <>
std::vector<TraceEvent> decode_canonical<std::vector<TraceEvent>>(std::span<const std::byte> bytes) {
    return decode_with<std::vector<TraceEvent>>(bytes, decode_trace_events);
}

template <>
std::vector<LawStatus> decode_canonical<std::vector<LawStatus>>(std::span<const std::byte> bytes) {
    return decode_with<std::vector<LawStatus>>(bytes, decode_law_statuses);
}

template <>
std::vector<LawViolation> decode_canonical<std::vector<LawViolation>>(std::span<const std::byte> bytes) {
    return decode_with<std::vector<LawViolation>>(bytes, decode_law_violations);
}

template <>
StoredCommit decode_canonical<StoredCommit>(std::span<const std::byte> bytes) {
    return decode_with<StoredCommit>(bytes, decode_stored_commit);
}

template <>
Program decode_canonical<Program>(std::span<const std::byte> bytes) {
    return decode_with<Program>(bytes, decode_program);
}

template <>
ObjectPage decode_canonical<ObjectPage>(std::span<const std::byte> bytes) {
    return decode_with<ObjectPage>(bytes, decode_object_page);
}

template <>
PointerPage decode_canonical<PointerPage>(std::span<const std::byte> bytes) {
    return decode_with<PointerPage>(bytes, decode_pointer_page);
}

template <>
FactPage decode_canonical<FactPage>(std::span<const std::byte> bytes) {
    return decode_with<FactPage>(bytes, decode_fact_page);
}

template <>
SymbolTableObject decode_canonical<SymbolTableObject>(std::span<const std::byte> bytes) {
    return decode_with<SymbolTableObject>(bytes, decode_symbol_table_object);
}

template <>
SnapshotPageIndexObject decode_canonical<SnapshotPageIndexObject>(std::span<const std::byte> bytes) {
    return decode_with<SnapshotPageIndexObject>(bytes, decode_snapshot_page_index_object);
}

template <>
SnapshotRootObject decode_canonical<SnapshotRootObject>(std::span<const std::byte> bytes) {
    return decode_with<SnapshotRootObject>(bytes, decode_snapshot_root_object);
}

}  // namespace pv
