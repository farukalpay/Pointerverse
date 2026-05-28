#include "pointerverse/analyzer.hpp"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>

namespace pointerverse {
namespace {

double max_probability_mass_drift(const Trace& trace) {
    if (trace.empty()) {
        return 0.0;
    }

    const auto baseline = trace.steps().front().before.total_probability_mass;
    double max_drift = 0.0;
    for (const auto& step : trace.steps()) {
        max_drift = std::max(max_drift, std::abs(step.after.total_probability_mass - baseline));
    }
    return max_drift;
}

bool relation_count_is_stable(const Trace& trace) {
    if (trace.empty()) {
        return false;
    }

    const auto baseline = trace.steps().front().before.relation_count;
    return std::all_of(trace.steps().begin(), trace.steps().end(), [baseline](const TraceStep& step) {
        return step.before.relation_count == baseline && step.after.relation_count == baseline;
    });
}

double max_normalization_error(const Trace& trace) {
    double result = 0.0;
    for (const auto& step : trace.steps()) {
        result = std::max(result, step.after.max_normalization_error);
    }
    return result;
}

}  // namespace

nlohmann::json AnalyzerFinding::to_json() const {
    return {
        {"kind", kind},
        {"message", message},
        {"confidence", confidence}
    };
}

bool AnalyzerReport::stable() const noexcept {
    return anomalies.empty();
}

nlohmann::json AnalyzerReport::to_json() const {
    nlohmann::json invariant_json = nlohmann::json::array();
    for (const auto& invariant : invariants) {
        invariant_json.push_back(invariant.to_json());
    }

    nlohmann::json anomaly_json = nlohmann::json::array();
    for (const auto& anomaly : anomalies) {
        anomaly_json.push_back(anomaly.to_json());
    }

    return {
        {"stable", stable()},
        {"invariants", std::move(invariant_json)},
        {"anomalies", std::move(anomaly_json)},
        {"suggested_experiments", suggested_experiments}
    };
}

AnalyzerReport Analyzer::scan(const Trace& trace) const {
    AnalyzerReport report;

    if (trace.empty()) {
        report.anomalies.push_back({
            "empty_trace",
            "no rewrite history exists; evolve the world before scanning",
            1.0
        });
        report.suggested_experiments.push_back("run evolve for at least one step, then scan again");
        return report;
    }

    const auto probability_drift = max_probability_mass_drift(trace);
    if (probability_drift <= 1e-9) {
        report.invariants.push_back({
            "total_probability_mass",
            fmt::format("total probability mass remained stable across {} rewrite step(s)", trace.size()),
            0.999
        });
    } else {
        report.anomalies.push_back({
            "probability_mass_drift",
            fmt::format("total probability mass drift reached {:.12g}", probability_drift),
            0.95
        });
    }

    if (relation_count_is_stable(trace)) {
        report.invariants.push_back({
            "relation_count",
            "semantic pointer count remained stable during rewrites",
            0.995
        });
    }

    const auto normalization_error = max_normalization_error(trace);
    if (normalization_error <= 1e-9) {
        report.invariants.push_back({
            "normalization",
            "all state vectors stayed normalized within tolerance",
            0.999
        });
    } else {
        report.anomalies.push_back({
            "normalization_error",
            fmt::format("maximum normalization error reached {:.12g}", normalization_error),
            0.98
        });
    }

    for (const auto& step : trace.steps()) {
        for (const auto& law : step.law_results) {
            if (!law.passed) {
                report.anomalies.push_back({
                    "law_violation",
                    fmt::format("law '{}' failed at step {}: {}", law.name, step.step, law.detail),
                    1.0
                });
            }
        }
    }

    if (report.anomalies.empty()) {
        report.suggested_experiments.push_back("compose non-commuting morphisms against equivalent initial states");
    } else {
        report.suggested_experiments.push_back("export trace JSON and inspect the first failing rewrite boundary");
    }

    return report;
}

AnalyzerReport Analyzer::scan_regions(const Trace& trace) const {
    AnalyzerReport report;
    if (trace.empty()) {
        report.anomalies.push_back({"empty_trace", "no rewrite history exists; evolve the world before scanning", 1.0});
        return report;
    }

    const auto& latest = trace.steps().back().after;
    if (latest.region_count == 0) {
        report.anomalies.push_back({"region_absence", "no regions have formed from persistent structural pressure", 0.8});
        report.suggested_experiments.push_back("seed contradictions or import conflict events, then evolve until pressure persists");
        return report;
    }

    report.invariants.push_back({
        "region_count",
        fmt::format("{} region(s) present in the latest snapshot", latest.region_count),
        0.99
    });
    report.invariants.push_back({
        "region_stability",
        fmt::format("average region stability is {:.6f}", latest.average_region_stability),
        latest.average_region_stability >= 0.5 ? 0.85 : 0.65
    });

    if (latest.max_pressure >= 0.75) {
        report.anomalies.push_back({
            "persistent_pressure",
            fmt::format("maximum structural pressure remains {:.6f}", latest.max_pressure),
            0.9
        });
    }

    report.suggested_experiments.push_back("inspect the highest-pressure region and trace its origin");
    return report;
}

AnalyzerReport Analyzer::scan_attractors(const Trace& trace) const {
    AnalyzerReport report;
    if (trace.empty()) {
        report.anomalies.push_back({"empty_trace", "no rewrite history exists; evolve the world before scanning", 1.0});
        return report;
    }

    const auto& latest = trace.steps().back().after;
    if (latest.attractor_count == 0) {
        report.anomalies.push_back({"attractor_absence", "no recurrent high-pressure structure is stable enough to count as an attractor", 0.7});
    } else {
        report.invariants.push_back({
            "attractor_candidates",
            fmt::format("{} recurrent structure(s) detected", latest.attractor_count),
            0.86
        });
    }
    report.suggested_experiments.push_back("compare repeated pattern imports against contradiction seeds");
    return report;
}

AnalyzerReport Analyzer::scan_law_drift(const Trace& trace) const {
    AnalyzerReport report;
    if (trace.empty()) {
        report.anomalies.push_back({"empty_trace", "no rewrite history exists; evolve the world before scanning", 1.0});
        return report;
    }

    double max_residual = 0.0;
    std::string law_name = "none";
    for (const auto& step : trace.steps()) {
        for (const auto& law : step.law_results) {
            if (std::abs(law.value) > max_residual) {
                max_residual = std::abs(law.value);
                law_name = law.name;
            }
        }
        for (const auto& event : step.structured_events) {
            for (const auto& [name, value] : event.law_residuals) {
                if (std::abs(value) > max_residual) {
                    max_residual = std::abs(value);
                    law_name = name;
                }
            }
        }
    }

    if (max_residual <= 1e-9) {
        report.invariants.push_back({"law_residuals", "law residuals remained at zero", 0.999});
    } else {
        report.anomalies.push_back({
            "law_drift",
            fmt::format("maximum residual {:.6f} observed for {}", max_residual, law_name),
            0.92
        });
    }
    report.suggested_experiments.push_back("locate the first structured trace event with non-zero residual");
    return report;
}

AnalyzerReport Analyzer::scan_noncommutativity(const Trace& trace) const {
    AnalyzerReport report;
    if (trace.empty()) {
        report.anomalies.push_back({"empty_trace", "no rewrite history exists; evolve the world before scanning", 1.0});
        return report;
    }

    double max_delta = 0.0;
    std::string morphism = "none";
    for (const auto& step : trace.steps()) {
        for (const auto& event : step.structured_events) {
            if (event.counterfactual_delta > max_delta) {
                max_delta = event.counterfactual_delta;
                morphism = event.morphism.empty() ? event.type : event.morphism;
            }
        }
    }

    if (max_delta <= 1e-9) {
        report.invariants.push_back({"composition_order", "no counterfactual isolation divergence detected", 0.95});
    } else {
        report.anomalies.push_back({
            "composition_order",
            fmt::format("counterfactual divergence {:.6f} detected near {}", max_delta, morphism),
            0.9
        });
    }
    report.suggested_experiments.push_back("apply split and stabilize in opposite orders on equivalent structures");
    return report;
}

}  // namespace pointerverse
