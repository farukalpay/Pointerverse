// SPDX-License-Identifier: Apache-2.0
#include "pv/storage/index_store.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>

#include "pv/hash/hasher.hpp"

namespace pv {
namespace {

constexpr std::uint32_t index_version = 1;

void append_u32(std::vector<std::byte>& out, std::uint32_t value) {
    for (std::size_t shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
    }
}

void append_u64(std::vector<std::byte>& out, std::uint64_t value) {
    for (std::size_t shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
    }
}

std::vector<std::byte> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open index file '" + path.string() + "'");
    }
    std::vector<std::byte> bytes;
    for (std::istreambuf_iterator<char> iter{input}, end; iter != end; ++iter) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(*iter)));
    }
    return bytes;
}

void write_file(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot write index file '" + path.string() + "'");
    }
    for (const auto byte : bytes) {
        output.put(static_cast<char>(byte));
    }
    output.flush();
    if (!output) {
        throw std::runtime_error("failed writing index file '" + path.string() + "'");
    }
}

std::uint32_t read_u32_from(std::span<const std::byte> bytes, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + index])) << (index * 8);
    }
    return value;
}

std::uint64_t read_u64_from(std::span<const std::byte> bytes, std::size_t offset) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(static_cast<unsigned char>(bytes[offset + index])) << (index * 8);
    }
    return value;
}

}  // namespace

void IndexPayloadWriter::u8(std::uint8_t value) {
    bytes_.push_back(static_cast<std::byte>(value));
}

void IndexPayloadWriter::u32(std::uint32_t value) {
    append_u32(bytes_, value);
}

void IndexPayloadWriter::u64(std::uint64_t value) {
    append_u64(bytes_, value);
}

void IndexPayloadWriter::boolean(bool value) {
    u8(value ? 1U : 0U);
}

void IndexPayloadWriter::hash(Hash256 value) {
    bytes_.insert(bytes_.end(), value.value.begin(), value.value.end());
}

void IndexPayloadWriter::string(std::string_view value) {
    u64(value.size());
    for (const auto ch : value) {
        bytes_.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }
}

std::span<const std::byte> IndexPayloadWriter::bytes() const noexcept {
    return bytes_;
}

std::vector<std::byte> IndexPayloadWriter::take() {
    return std::move(bytes_);
}

IndexPayloadReader::IndexPayloadReader(std::span<const std::byte> bytes) : bytes_(bytes) {}

std::span<const std::byte> IndexPayloadReader::read(std::size_t size) {
    if (offset_ + size > bytes_.size()) {
        throw std::runtime_error("truncated index payload");
    }
    const auto out = bytes_.subspan(offset_, size);
    offset_ += size;
    return out;
}

std::uint8_t IndexPayloadReader::u8() {
    return static_cast<std::uint8_t>(static_cast<unsigned char>(read(1).front()));
}

std::uint32_t IndexPayloadReader::u32() {
    const auto bytes = read(4);
    return read_u32_from(bytes, 0);
}

std::uint64_t IndexPayloadReader::u64() {
    const auto bytes = read(8);
    return read_u64_from(bytes, 0);
}

bool IndexPayloadReader::boolean() {
    const auto value = u8();
    if (value > 1) {
        throw std::runtime_error("invalid boolean in index payload");
    }
    return value == 1;
}

Hash256 IndexPayloadReader::hash() {
    const auto bytes = read(32);
    Hash256 out;
    std::ranges::copy(bytes, out.value.begin());
    return out;
}

std::string IndexPayloadReader::string() {
    const auto size = u64();
    const auto bytes = read(static_cast<std::size_t>(size));
    std::string out;
    out.reserve(bytes.size());
    for (const auto byte : bytes) {
        out.push_back(static_cast<char>(byte));
    }
    return out;
}

void IndexPayloadReader::expect_end() const {
    if (offset_ != bytes_.size()) {
        throw std::runtime_error("index payload has trailing bytes");
    }
}

IndexStore::IndexStore(std::filesystem::path root, std::string file_name, std::string magic)
    : root_(std::move(root)), file_name_(std::move(file_name)), magic_(std::move(magic)) {}

const std::filesystem::path& IndexStore::root() const noexcept {
    return root_;
}

std::filesystem::path IndexStore::path() const {
    return root_ / "index" / file_name_;
}

bool IndexStore::exists() const {
    return std::filesystem::exists(path());
}

std::vector<std::byte> IndexStore::read_payload() const {
    const auto bytes = read_file(path());
    const auto header_size = magic_.size() + 4 + 8;
    if (bytes.size() < header_size + 32) {
        throw std::runtime_error("index file is truncated: " + path().string());
    }
    for (std::size_t index = 0; index < magic_.size(); ++index) {
        if (bytes[index] != static_cast<std::byte>(static_cast<unsigned char>(magic_[index]))) {
            throw std::runtime_error("index file has invalid magic: " + path().string());
        }
    }
    const auto version = read_u32_from(bytes, magic_.size());
    if (version != index_version) {
        throw std::runtime_error("index file has unsupported version: " + path().string());
    }
    const auto payload_size = read_u64_from(bytes, magic_.size() + 4);
    if (payload_size > bytes.size() || header_size + payload_size + 32 != bytes.size()) {
        throw std::runtime_error("index file has invalid payload length: " + path().string());
    }

    const auto payload_begin = bytes.begin() + static_cast<std::ptrdiff_t>(header_size);
    const auto payload_end = payload_begin + static_cast<std::ptrdiff_t>(payload_size);
    std::vector<std::byte> payload(payload_begin, payload_end);

    Hash256 expected;
    std::ranges::copy(
        std::span<const std::byte>{bytes}.subspan(header_size + static_cast<std::size_t>(payload_size), 32),
        expected.value.begin());
    if (sha256(payload) != expected) {
        throw std::runtime_error("index file checksum mismatch: " + path().string());
    }
    return payload;
}

IndexFileStatus IndexStore::check() const {
    IndexFileStatus status;
    status.exists = exists();
    if (!status.exists) {
        return status;
    }
    try {
        const auto payload = read_payload();
        status.payload_bytes = payload.size();
        status.checksum = sha256(payload);
        status.checksum_ok = true;
    } catch (const std::exception& error) {
        status.error = error.what();
    }
    return status;
}

Hash256 IndexStore::checksum() const {
    return sha256(read_payload());
}

void IndexStore::write_payload(std::span<const std::byte> payload) const {
    std::vector<std::byte> bytes;
    bytes.reserve(magic_.size() + 4 + 8 + payload.size() + 32);
    for (const auto ch : magic_) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }
    append_u32(bytes, index_version);
    append_u64(bytes, payload.size());
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    const auto sum = sha256(payload);
    bytes.insert(bytes.end(), sum.value.begin(), sum.value.end());

    std::filesystem::create_directories(path().parent_path());
    const auto tmp = path().string() + ".tmp";
    write_file(tmp, bytes);
    std::filesystem::rename(tmp, path());
}

void IndexStore::remove() const {
    std::filesystem::remove(path());
}

}  // namespace pv
