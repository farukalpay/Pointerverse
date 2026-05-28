// SPDX-License-Identifier: Apache-2.0
#include "pv/hash/hasher.hpp"

#include <openssl/evp.h>

#include <array>
#include <limits>
#include <stdexcept>

namespace pv {
namespace {

void write_big_endian(std::vector<std::byte>& bytes, std::uint64_t value, std::size_t width) {
    for (std::size_t shift = width; shift > 0; --shift) {
        bytes.push_back(static_cast<std::byte>((value >> ((shift - 1U) * 8U)) & 0xffU));
    }
}

}  // namespace

void CanonicalHasher::write_u8(std::uint8_t value) {
    bytes_.push_back(static_cast<std::byte>(value));
}

void CanonicalHasher::write_u32(std::uint32_t value) {
    write_big_endian(bytes_, value, 4);
}

void CanonicalHasher::write_u64(std::uint64_t value) {
    write_big_endian(bytes_, value, 8);
}

void CanonicalHasher::write_i64(std::int64_t value) {
    write_u64(static_cast<std::uint64_t>(value));
}

void CanonicalHasher::write_string(std::string_view value) {
    write_u64(value.size());
    for (const auto ch : value) {
        bytes_.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }
}

void CanonicalHasher::write_f64(double value) {
    write_u64(canonical_f64(value));
}

void CanonicalHasher::write_hash(Hash256 hash) {
    write_bytes(hash.value);
}

void CanonicalHasher::write_bytes(std::span<const std::byte> bytes) {
    bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
}

Hash256 CanonicalHasher::finish() const {
    Hash256 out;
    unsigned int length = 0;
    if (EVP_Digest(
            bytes_.data(),
            bytes_.size(),
            reinterpret_cast<unsigned char*>(out.value.data()),
            &length,
            EVP_sha256(),
            nullptr)
        != 1) {
        throw std::runtime_error("OpenSSL SHA-256 digest failed");
    }
    if (length != out.value.size()) {
        throw std::runtime_error("OpenSSL SHA-256 produced an unexpected digest length");
    }
    return out;
}

}  // namespace pv
