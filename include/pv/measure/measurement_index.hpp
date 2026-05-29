// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pv/hash/canonical.hpp"
#include "pv/measure/risk_value.hpp"
#include "pv/runtime/ids.hpp"
#include "pv/storage/index_store.hpp"

namespace pv {

struct MeasurementIndexEntry {
    std::string branch;
    CommitId commit;
    Hash256 spec_hash;
    Hash256 measurement_object;
    RiskVector risk;
    std::uint64_t projection{0};

    friend bool operator==(const MeasurementIndexEntry&, const MeasurementIndexEntry&) = default;
};

class MeasurementIndex {
public:
    explicit MeasurementIndex(std::filesystem::path root);

    [[nodiscard]] bool exists() const;
    [[nodiscard]] std::vector<MeasurementIndexEntry> entries() const;
    [[nodiscard]] std::optional<MeasurementIndexEntry> find(CommitId commit, Hash256 spec) const;
    [[nodiscard]] std::optional<MeasurementIndexEntry> find(
        std::string_view branch,
        CommitId commit,
        Hash256 spec) const;
    [[nodiscard]] std::vector<MeasurementIndexEntry> branch_entries(
        std::string_view branch,
        Hash256 spec) const;
    [[nodiscard]] IndexFileStatus check() const;
    [[nodiscard]] Hash256 checksum() const;

    void write(std::vector<MeasurementIndexEntry> entries) const;
    void upsert(std::string_view branch, MeasurementIndexEntry entry) const;
    void remove_branch_spec(std::string_view branch, Hash256 spec) const;
    void remove() const;

private:
    IndexStore store_;
};

}  // namespace pv
