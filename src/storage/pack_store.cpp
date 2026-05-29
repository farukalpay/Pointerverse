// SPDX-License-Identifier: Apache-2.0
#include "pv/storage/pack_store.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "pv/hash/hasher.hpp"

namespace pv {
namespace {

std::vector<std::byte> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open packed object '" + path.string() + "'");
    }
    std::vector<std::byte> bytes;
    for (std::istreambuf_iterator<char> iter{input}, end; iter != end; ++iter) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(*iter)));
    }
    return bytes;
}

std::vector<std::byte> loose_object_bytes(const std::filesystem::path& path) {
    const auto bytes = read_file(path);
    const auto parsed = parse_hash256(path.filename().string());
    if (!parsed.has_value() || sha256(bytes) != *parsed) {
        throw std::runtime_error("loose object hash mismatch: " + path.string());
    }
    return bytes;
}

std::vector<std::filesystem::path> loose_object_paths(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> paths;
    const auto object_root = root / "objects";
    if (!std::filesystem::exists(object_root)) {
        return paths;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(object_root)) {
        if (!entry.is_regular_file() || entry.path().extension() == ".tmp") {
            continue;
        }
        if (parse_hash256(entry.path().filename().string()).has_value()) {
            paths.push_back(entry.path());
        }
    }
    std::ranges::sort(paths);
    return paths;
}

std::string pack_name(std::uint64_t number, std::string_view extension) {
    std::ostringstream out;
    out << "pack-" << std::setw(6) << std::setfill('0') << number << extension;
    return out.str();
}

}  // namespace

PackedContentStore::PackedContentStore(std::filesystem::path root) : root_(std::move(root)) {}

std::filesystem::path PackedContentStore::packs_root() const {
    return root_ / "packs";
}

std::vector<PackedContentStore::PackIndexEntry> PackedContentStore::index_entries() const {
    std::vector<PackIndexEntry> entries;
    if (!std::filesystem::exists(packs_root())) {
        return entries;
    }
    for (const auto& index : std::filesystem::directory_iterator(packs_root())) {
        if (!index.is_regular_file() || index.path().extension() != ".idx") {
            continue;
        }
        const auto pack_path = index.path().parent_path() / index.path().filename().replace_extension(".pvp");
        std::ifstream input(index.path());
        std::string magic;
        input >> magic;
        if (magic != "PVIDX1") {
            continue;
        }
        std::string hash_text;
        std::uint64_t offset = 0;
        std::uint64_t size = 0;
        while (input >> hash_text >> offset >> size) {
            const auto hash = parse_hash256(hash_text);
            if (!hash.has_value()) {
                throw std::runtime_error("invalid hash in pack index: " + index.path().string());
            }
            entries.push_back(PackIndexEntry{*hash, pack_path, offset, size});
        }
    }
    return entries;
}

std::optional<PackedContentStore::PackIndexEntry> PackedContentStore::find(Hash256 id) const {
    for (const auto& entry : index_entries()) {
        if (entry.id == id) {
            return entry;
        }
    }
    return std::nullopt;
}

bool PackedContentStore::contains(Hash256 id) const {
    return find(id).has_value();
}

std::size_t PackedContentStore::packed_object_count() const {
    return index_entries().size();
}

std::vector<std::byte> PackedContentStore::get_bytes(Hash256 id) const {
    const auto entry = find(id);
    if (!entry.has_value()) {
        throw std::runtime_error("object not found in pack store: " + to_hex(id));
    }
    std::ifstream input(entry->pack_path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open pack file '" + entry->pack_path.string() + "'");
    }
    input.seekg(static_cast<std::streamoff>(entry->offset), std::ios::beg);
    std::vector<std::byte> bytes(static_cast<std::size_t>(entry->size));
    for (auto& byte : bytes) {
        char ch = 0;
        input.get(ch);
        if (!input) {
            throw std::runtime_error("truncated pack object '" + entry->pack_path.string() + "'");
        }
        byte = static_cast<std::byte>(static_cast<unsigned char>(ch));
    }
    if (sha256(bytes) != id) {
        throw std::runtime_error("packed object hash mismatch: " + to_hex(id));
    }
    return bytes;
}

std::uint64_t PackedContentStore::next_pack_number() const {
    std::uint64_t next = 1;
    if (!std::filesystem::exists(packs_root())) {
        return next;
    }
    for (const auto& entry : std::filesystem::directory_iterator(packs_root())) {
        if (!entry.is_regular_file() || entry.path().extension() != ".pvp") {
            continue;
        }
        const auto stem = entry.path().stem().string();
        if (!stem.starts_with("pack-")) {
            continue;
        }
        try {
            next = std::max(next, static_cast<std::uint64_t>(std::stoull(stem.substr(5)) + 1));
        } catch (const std::exception&) {
        }
    }
    return next;
}

PackReport PackedContentStore::compact_loose_objects() {
    const auto loose = loose_object_paths(root_);
    PackReport report;
    if (loose.empty()) {
        return report;
    }

    std::filesystem::create_directories(packs_root());
    const auto number = next_pack_number();
    report.pack_path = packs_root() / pack_name(number, ".pvp");
    report.index_path = packs_root() / pack_name(number, ".idx");

    const auto tmp_pack = report.pack_path.string() + ".tmp";
    const auto tmp_index = report.index_path.string() + ".tmp";
    std::ofstream pack(tmp_pack, std::ios::binary | std::ios::trunc);
    std::ofstream index(tmp_index, std::ios::trunc);
    if (!pack || !index) {
        throw std::runtime_error("cannot create pack files");
    }

    pack << "PVPACK1\n";
    index << "PVIDX1\n";
    std::uint64_t offset = static_cast<std::uint64_t>(pack.tellp());
    for (const auto& path : loose) {
        const auto bytes = loose_object_bytes(path);
        const auto hash = *parse_hash256(path.filename().string());
        index << to_hex(hash) << ' ' << offset << ' ' << bytes.size() << '\n';
        for (const auto byte : bytes) {
            pack.put(static_cast<char>(byte));
        }
        offset += bytes.size();
        report.packed_objects += 1;
        report.packed_bytes += bytes.size();
    }
    pack.flush();
    index.flush();
    if (!pack || !index) {
        throw std::runtime_error("failed writing pack files");
    }
    pack.close();
    index.close();
    std::filesystem::rename(tmp_pack, report.pack_path);
    std::filesystem::rename(tmp_index, report.index_path);

    for (const auto& path : loose) {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
    return report;
}

}  // namespace pv
