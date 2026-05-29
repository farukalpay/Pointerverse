// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "pv/hash/canonical.hpp"
#include "pv/law/verifier.hpp"
#include "pv/measure/measurement_index.hpp"
#include "pv/measure/measurement_spec.hpp"
#include "pv/runtime/ids.hpp"
#include "pv/storage/index_store.hpp"

namespace pv {

class CanonicalReader;
class CanonicalWriter;
class Repository;

struct CalibrationBaseline {
    std::string branch;
    CommitId up_to_commit;
    Hash256 spec_hash;
    std::vector<MeasurementIndexEntry> sample;
    Hash256 baseline_hash;

    friend bool operator==(const CalibrationBaseline&, const CalibrationBaseline&) = default;
};

struct CalibrationBaselineRef {
    std::string name;
    Hash256 baseline_object;
};

struct ComponentStats {
    std::string namespace_id;
    std::string component_id;
    std::uint64_t median{0};
    std::uint64_t mad{1};
    std::uint64_t q80{0};
    std::uint64_t q95{0};
    std::uint64_t q99{0};
    std::uint64_t max_seen{0};

    friend bool operator==(const ComponentStats&, const ComponentStats&) = default;
};

struct CalibrationProfile {
    Hash256 baseline_hash;
    std::vector<ComponentStats> components;
    Hash256 profile_hash;

    friend bool operator==(const CalibrationProfile&, const CalibrationProfile&) = default;
};

[[nodiscard]] Hash256 calibration_baseline_hash(const CalibrationBaseline& baseline);
void encode(CanonicalWriter& writer, const CalibrationBaseline& baseline);
[[nodiscard]] CalibrationBaseline decode_calibration_baseline(CanonicalReader& reader);
[[nodiscard]] CalibrationBaseline decode_calibration_baseline_bytes(std::span<const std::byte> bytes);
[[nodiscard]] Hash256 calibration_profile_hash(const CalibrationProfile& profile);
void encode(CanonicalWriter& writer, const CalibrationProfile& profile);
[[nodiscard]] CalibrationProfile decode_calibration_profile(CanonicalReader& reader);
[[nodiscard]] CalibrationProfile decode_calibration_profile_bytes(std::span<const std::byte> bytes);
[[nodiscard]] CalibrationProfile calibration_profile_from_baseline(
    const Repository& repository,
    const CalibrationBaseline& baseline);
[[nodiscard]] ProjectionPolicy calibrated_projection_policy(
    const CalibrationProfile& profile,
    std::string calibration_mode = "robust_z");

class CalibrationStore {
public:
    explicit CalibrationStore(Repository& repository);

    [[nodiscard]] std::optional<CalibrationBaselineRef> find_ref(std::string_view name) const;
    [[nodiscard]] CalibrationBaseline load(std::string_view name) const;
    [[nodiscard]] CalibrationBaseline load(Hash256 baseline_object) const;
    [[nodiscard]] CalibrationBaseline create(
        std::string_view name,
        std::string_view branch,
        CommitId up_to_commit,
        const MeasurementSpec& spec,
        const Verifier* verifier = nullptr) const;

private:
    [[nodiscard]] std::vector<CalibrationBaselineRef> refs() const;
    void write_refs(std::vector<CalibrationBaselineRef> refs) const;
    void save_ref(std::string_view name, Hash256 baseline_object) const;

    Repository& repository_;
    IndexStore refs_;
};

[[nodiscard]] bool calibration_contains_commit(const CalibrationBaseline& baseline, CommitId commit) noexcept;

}  // namespace pv
