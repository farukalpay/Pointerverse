// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
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

[[nodiscard]] Hash256 calibration_baseline_hash(const CalibrationBaseline& baseline);
void encode(CanonicalWriter& writer, const CalibrationBaseline& baseline);
[[nodiscard]] CalibrationBaseline decode_calibration_baseline(CanonicalReader& reader);
[[nodiscard]] CalibrationBaseline decode_calibration_baseline_bytes(std::span<const std::byte> bytes);

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
