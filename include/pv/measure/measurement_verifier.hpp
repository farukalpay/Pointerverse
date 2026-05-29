// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "pv/law/verifier.hpp"
#include "pv/measure/measurement_spec.hpp"

namespace pv {

class Repository;

struct MeasurementVerificationError {
    std::string message;
};

struct MeasurementVerificationReport {
    std::string branch;
    std::size_t measurements_checked{0};
    Hash256 spec_hash;
    std::vector<MeasurementVerificationError> errors;
    std::vector<std::string> warnings;

    [[nodiscard]] bool clean() const noexcept { return errors.empty(); }
};

struct MeasurementVerificationOptions {
    bool strict_cache{false};
};

class MeasurementVerifier {
public:
    explicit MeasurementVerifier(Repository& repository);

    [[nodiscard]] MeasurementVerificationReport verify_branch(
        std::string_view branch,
        const MeasurementSpec& spec,
        const Verifier* verifier = nullptr,
        MeasurementVerificationOptions options = {}) const;

private:
    Repository& repository_;
};

[[nodiscard]] std::string render_measurement_verification_text(
    const MeasurementVerificationReport& report,
    const MeasurementSpec& spec);

}  // namespace pv
