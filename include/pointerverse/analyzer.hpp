#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "pointerverse/trace.hpp"

namespace pointerverse {

struct AnalyzerFinding {
    std::string kind;
    std::string message;
    double confidence{0.0};

    [[nodiscard]] nlohmann::json to_json() const;
};

struct AnalyzerReport {
    std::vector<AnalyzerFinding> invariants;
    std::vector<AnalyzerFinding> anomalies;
    std::vector<std::string> suggested_experiments;

    [[nodiscard]] bool stable() const noexcept;
    [[nodiscard]] nlohmann::json to_json() const;
};

class Analyzer {
public:
    [[nodiscard]] AnalyzerReport scan(const Trace& trace) const;
    [[nodiscard]] AnalyzerReport scan_regions(const Trace& trace) const;
    [[nodiscard]] AnalyzerReport scan_attractors(const Trace& trace) const;
    [[nodiscard]] AnalyzerReport scan_law_drift(const Trace& trace) const;
    [[nodiscard]] AnalyzerReport scan_noncommutativity(const Trace& trace) const;
};

}  // namespace pointerverse
