// SPDX-License-Identifier: Apache-2.0
#include "pv/measure/law_measure.hpp"

#include <cmath>
#include <limits>
#include <sstream>

namespace pv {

std::uint64_t canonical_fixed_magnitude(double magnitude) noexcept {
    if (!std::isfinite(magnitude)) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    const auto absolute = std::abs(magnitude);
    if (absolute <= 0.0) {
        return 0;
    }
    if (absolute >= static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return static_cast<std::uint64_t>(std::ceil(absolute));
}

MeasuredComponent LawRiskMeasure::measure(const CommitRecord& record) const {
    MeasuredComponent component;
    component.name = "law";
    component.evidence.component = component.name;
    component.evidence.input_root = record.before_root;
    component.evidence.output_root = record.violation_hash;
    component.evidence.commits.push_back(record.id);

    for (const auto& violation : record.violations) {
        const auto value = canonical_fixed_magnitude(violation.magnitude);
        if (std::numeric_limits<std::uint64_t>::max() - component.value < value) {
            component.value = std::numeric_limits<std::uint64_t>::max();
        } else {
            component.value += value;
        }
        component.evidence.laws.push_back(violation.law);
        component.evidence.objects.insert(
            component.evidence.objects.end(),
            violation.objects.begin(),
            violation.objects.end());
        component.evidence.pointers.insert(
            component.evidence.pointers.end(),
            violation.pointers.begin(),
            violation.pointers.end());
    }

    std::ostringstream explanation;
    explanation << "violated laws: " << record.violations.size()
                << "; total magnitude: " << component.value;
    component.evidence.explanation = explanation.str();
    return component;
}

}  // namespace pv

