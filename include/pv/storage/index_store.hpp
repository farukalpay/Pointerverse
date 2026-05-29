// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "pv/hash/canonical.hpp"

namespace pv {

struct IndexFileStatus {
    bool exists{false};
    bool checksum_ok{false};
    std::size_t payload_bytes{0};
    Hash256 checksum;
    std::string error;
};

class IndexPayloadWriter {
public:
    void u8(std::uint8_t value);
    void u32(std::uint32_t value);
    void u64(std::uint64_t value);
    void boolean(bool value);
    void hash(Hash256 value);
    void string(std::string_view value);

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept;
    [[nodiscard]] std::vector<std::byte> take();

private:
    std::vector<std::byte> bytes_;
};

class IndexPayloadReader {
public:
    explicit IndexPayloadReader(std::span<const std::byte> bytes);

    [[nodiscard]] std::uint8_t u8();
    [[nodiscard]] std::uint32_t u32();
    [[nodiscard]] std::uint64_t u64();
    [[nodiscard]] bool boolean();
    [[nodiscard]] Hash256 hash();
    [[nodiscard]] std::string string();
    void expect_end() const;

private:
    [[nodiscard]] std::span<const std::byte> read(std::size_t size);

    std::span<const std::byte> bytes_;
    std::size_t offset_{0};
};

class IndexStore {
public:
    IndexStore(std::filesystem::path root, std::string file_name, std::string magic);

    [[nodiscard]] const std::filesystem::path& root() const noexcept;
    [[nodiscard]] std::filesystem::path path() const;
    [[nodiscard]] bool exists() const;
    [[nodiscard]] std::vector<std::byte> read_payload() const;
    [[nodiscard]] IndexFileStatus check() const;
    [[nodiscard]] Hash256 checksum() const;

    void write_payload(std::span<const std::byte> payload) const;
    void remove() const;

private:
    std::filesystem::path root_;
    std::string file_name_;
    std::string magic_;
};

}  // namespace pv
