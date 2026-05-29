// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "pv/hash/canonical.hpp"
#include "pv/measure/risk_value.hpp"
#include "pv/storage/index_store.hpp"

namespace pv {

class CanonicalReader;
class CanonicalWriter;

struct ProjectionTerm {
    std::string namespace_id;
    std::string component_id;
    std::uint64_t weight_num{1};
    std::uint64_t weight_den{1};

    friend bool operator==(const ProjectionTerm&, const ProjectionTerm&) = default;
};

struct ProjectionPolicy {
    std::string id{"pointerverse.default_projection"};
    std::uint32_t version{1};
    std::vector<ProjectionTerm> terms;

    friend bool operator==(const ProjectionPolicy&, const ProjectionPolicy&) = default;
};

struct RiskProjection {
    std::uint64_t structural_weight{1};
    std::uint64_t law_weight{1};
    std::uint64_t repair_weight{1};
    std::uint64_t surprise_weight{1};
};

struct ProjectionRecord {
    Hash256 measurement_hash;
    Hash256 projection_policy_hash;
    std::uint64_t projected_score{0};
    std::string decision;
    Hash256 baseline_hash;
    Hash256 projection_hash;

    friend bool operator==(const ProjectionRecord&, const ProjectionRecord&) = default;
};

using ProjectionResult = ProjectionRecord;

struct ProjectionIndexEntry {
    Hash256 measurement_hash;
    Hash256 projection_policy_hash;
    Hash256 projection_object;
    std::uint64_t projected_score{0};
    std::string decision;
    Hash256 baseline_hash;

    friend bool operator==(const ProjectionIndexEntry&, const ProjectionIndexEntry&) = default;
};

class ProjectionIndex {
public:
    explicit ProjectionIndex(std::filesystem::path root);

    [[nodiscard]] bool exists() const;
    [[nodiscard]] std::vector<ProjectionIndexEntry> entries() const;
    [[nodiscard]] std::optional<ProjectionIndexEntry> find(Hash256 measurement, Hash256 policy) const;
    [[nodiscard]] IndexFileStatus check() const;
    [[nodiscard]] Hash256 checksum() const;

    void write(std::vector<ProjectionIndexEntry> entries) const;
    void upsert(ProjectionIndexEntry entry) const;
    void remove() const;

private:
    IndexStore store_;
};

[[nodiscard]] ProjectionPolicy default_projection_policy();
[[nodiscard]] ProjectionPolicy projection_policy_from_legacy(RiskProjection projection);
[[nodiscard]] std::uint64_t project(RiskVector value, RiskProjection projection = {}) noexcept;
[[nodiscard]] std::uint64_t project(const RiskLatticeElement& value, ProjectionPolicy projection) noexcept;
[[nodiscard]] Hash256 projection_policy_hash(ProjectionPolicy projection);
[[nodiscard]] Hash256 projection_policy_hash(RiskProjection projection);
[[nodiscard]] Hash256 projection_result_hash(const ProjectionRecord& result);
[[nodiscard]] ProjectionResult make_projection_result(
    Hash256 measurement_hash,
    RiskVector value,
    RiskProjection projection,
    std::string decision = {},
    Hash256 baseline_hash = {});
[[nodiscard]] ProjectionRecord make_projection_result(
    Hash256 measurement_hash,
    const RiskLatticeElement& value,
    ProjectionPolicy projection,
    std::string decision = {},
    Hash256 baseline_hash = {});

void encode(CanonicalWriter& writer, const ProjectionPolicy& policy);
void encode(CanonicalWriter& writer, const ProjectionRecord& record);
[[nodiscard]] ProjectionPolicy decode_projection_policy(CanonicalReader& reader);
[[nodiscard]] ProjectionPolicy decode_projection_policy_bytes(std::span<const std::byte> bytes);
[[nodiscard]] ProjectionRecord decode_projection_record(CanonicalReader& reader);
[[nodiscard]] ProjectionRecord decode_projection_record_bytes(std::span<const std::byte> bytes);

}  // namespace pv
