// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <vector>

#include "pv/hash/canonical.hpp"
#include "pv/storage/canonical_codec.hpp"
#include "pv/storage/object_codec.hpp"

namespace pv {

template <class T>
[[nodiscard]] T decode_canonical(std::span<const std::byte> bytes);

template <>
[[nodiscard]] WorldSnapshot decode_canonical<WorldSnapshot>(std::span<const std::byte> bytes);
template <>
[[nodiscard]] Delta decode_canonical<Delta>(std::span<const std::byte> bytes);
template <>
[[nodiscard]] std::vector<TraceEvent> decode_canonical<std::vector<TraceEvent>>(std::span<const std::byte> bytes);
template <>
[[nodiscard]] std::vector<LawStatus> decode_canonical<std::vector<LawStatus>>(std::span<const std::byte> bytes);
template <>
[[nodiscard]] std::vector<LawViolation> decode_canonical<std::vector<LawViolation>>(std::span<const std::byte> bytes);
template <>
[[nodiscard]] StoredCommit decode_canonical<StoredCommit>(std::span<const std::byte> bytes);
template <>
[[nodiscard]] Program decode_canonical<Program>(std::span<const std::byte> bytes);

class ContentStore {
public:
    explicit ContentStore(std::filesystem::path root);

    [[nodiscard]] const std::filesystem::path& root() const noexcept;
    [[nodiscard]] std::filesystem::path object_path(Hash256 id) const;

    [[nodiscard]] Hash256 put_bytes(std::span<const std::byte> bytes);
    [[nodiscard]] std::vector<std::byte> get_bytes(Hash256 id) const;
    [[nodiscard]] bool contains(Hash256 id) const;

    template <class T>
    [[nodiscard]] Hash256 put_canonical(const T& value) {
        return put_bytes(canonical_encode(value));
    }

    template <class T>
    [[nodiscard]] T get_canonical(Hash256 id) const {
        return decode_canonical<T>(get_bytes(id));
    }

private:
    std::filesystem::path root_;
};

}  // namespace pv
