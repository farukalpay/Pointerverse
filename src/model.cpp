#include "pointerverse/model.hpp"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <stdexcept>

namespace pointerverse {
namespace {

double clamp01(double value) {
    if (!std::isfinite(value)) {
        return 0.0;
    }
    return std::clamp(value, 0.0, 1.0);
}

std::string json_string_or(const nlohmann::json& json, const std::string& key, const std::string& fallback) {
    const auto iter = json.find(key);
    if (iter == json.end() || !iter->is_string()) {
        return fallback;
    }
    return iter->get<std::string>();
}

double json_double_or(const nlohmann::json& json, const std::string& key, double fallback) {
    const auto iter = json.find(key);
    if (iter == json.end() || !iter->is_number()) {
        return fallback;
    }
    return iter->get<double>();
}

}  // namespace

bool StructuralPressure::active(double threshold) const noexcept {
    return magnitude > threshold
        || persistence > threshold
        || locality > threshold
        || recurrence > threshold
        || instability > threshold;
}

nlohmann::json StructuralPressure::to_json() const {
    return {
        {"magnitude", magnitude},
        {"persistence", persistence},
        {"locality", locality},
        {"recurrence", recurrence},
        {"instability", instability}
    };
}

nlohmann::json RegionBoundary::to_json() const {
    return {
        {"internal_links", internal_links},
        {"external_links", external_links},
        {"internal_density", internal_density},
        {"boundary_density", boundary_density},
        {"permeability", permeability}
    };
}

nlohmann::json Region::to_json() const {
    nlohmann::json object_json = nlohmann::json::array();
    for (const auto& object : objects) {
        object_json.push_back(to_string(object));
    }

    nlohmann::json link_json = nlohmann::json::array();
    for (const auto& link : internal_links) {
        link_json.push_back(to_string(link));
    }

    return {
        {"id", to_string(id)},
        {"name", name},
        {"objects", std::move(object_json)},
        {"internal_links", std::move(link_json)},
        {"boundary", boundary.to_json()},
        {"pressure", pressure.to_json()},
        {"stability", stability},
        {"origin", origin}
    };
}

nlohmann::json SymbolicCompression::to_json() const {
    return {
        {"symbol_name", symbol_name},
        {"object_type", object_type},
        {"relation", relation},
        {"interpretation", interpretation},
        {"pressure", pressure.to_json()},
        {"creates_constraint", creates_constraint},
        {"creates_attractor_candidate", creates_attractor_candidate},
        {"creates_gap", creates_gap}
    };
}

nlohmann::json InternalizationResult::to_json() const {
    nlohmann::json object_json = nlohmann::json::array();
    for (const auto& object : objects) {
        object_json.push_back(to_string(object));
    }

    nlohmann::json pointer_json = nlohmann::json::array();
    for (const auto& pointer : pointers) {
        pointer_json.push_back(to_string(pointer));
    }

    return {
        {"objects", std::move(object_json)},
        {"pointers", std::move(pointer_json)},
        {"pressure", pressure.to_json()},
        {"events", events}
    };
}

nlohmann::json MorphismApplicationResult::to_json() const {
    return {
        {"applied", applied},
        {"valid", valid},
        {"morphism", morphism},
        {"target", target},
        {"reason", reason},
        {"pressure_delta", pressure_delta},
        {"law_residual", law_residual},
        {"counterfactual_delta", counterfactual_delta},
        {"events", events}
    };
}

StructuralPressure compute_pressure(
    double law_residual,
    double graph_entropy,
    double state_drift,
    double noncommutativity,
    const PressureWeights& weights) {
    const auto law = clamp01(law_residual);
    const auto entropy = clamp01(graph_entropy);
    const auto drift = clamp01(state_drift);
    const auto noncommuting = clamp01(noncommutativity);
    const auto magnitude = clamp01(
        weights.law_residual * law
        + weights.graph_entropy * entropy
        + weights.state_drift * drift
        + weights.noncommutativity * noncommuting);

    return StructuralPressure{
        magnitude,
        clamp01(law * 0.7 + noncommuting * 0.3),
        clamp01(1.0 - entropy),
        clamp01(noncommuting),
        clamp01(drift * 0.6 + law * 0.4)
    };
}

SymbolicCompression compress_external_event(const ExternalEvent& event) {
    if (event.id.empty()) {
        throw std::invalid_argument("external event id cannot be empty");
    }
    if (event.kind.empty()) {
        throw std::invalid_argument("external event kind cannot be empty");
    }

    const auto metric_or = [&event](const std::string& key, double fallback) {
        const auto iter = event.metrics.find(key);
        return iter == event.metrics.end() ? fallback : iter->second;
    };

    SymbolicCompression compression;
    compression.symbol_name = event.id;
    compression.pressure = compute_pressure(
        metric_or("law_residual", event.kind == "conflict" ? event.weight : 0.0),
        metric_or("graph_entropy", 0.25 * event.weight),
        metric_or("state_drift", event.kind == "unstable_boundary" ? event.weight : 0.0),
        metric_or("noncommutativity", event.kind == "composition_failure" ? event.weight : 0.0));

    if (event.kind == "conflict" || event.kind == "measured_conflict") {
        compression.object_type = "UnresolvedConstraint";
        compression.relation = "contradicts";
        compression.interpretation = "measured conflict compressed into unresolved constraint";
        compression.creates_constraint = true;
    } else if (event.kind == "repeated_pattern") {
        compression.object_type = "AttractorCandidate";
        compression.relation = "recurs_with";
        compression.interpretation = "repeated pattern compressed into attractor candidate";
        compression.creates_attractor_candidate = true;
    } else if (event.kind == "missing_relation") {
        compression.object_type = "StructuralGap";
        compression.relation = "requires_relation";
        compression.interpretation = "missing relation compressed into structural gap";
        compression.creates_gap = true;
    } else if (event.kind == "unstable_boundary") {
        compression.object_type = "BoundaryInstability";
        compression.relation = "separates";
        compression.interpretation = "unstable boundary compressed into region separator";
    } else {
        compression.object_type = "SymbolicEvent";
        compression.relation = "projects_to";
        compression.interpretation = fmt::format("{} compressed into symbolic event", event.kind);
    }

    return compression;
}

ExternalEvent external_event_from_json(const nlohmann::json& json) {
    if (!json.is_object()) {
        throw std::invalid_argument("external event must be an object");
    }

    ExternalEvent event;
    event.id = json_string_or(json, "id", "");
    event.kind = json_string_or(json, "kind", "");
    event.weight = json_double_or(json, "weight", 1.0);

    if (const auto iter = json.find("metrics"); iter != json.end()) {
        if (!iter->is_object()) {
            throw std::invalid_argument("external event metrics must be an object");
        }
        for (const auto& [key, value] : iter->items()) {
            if (value.is_number()) {
                event.metrics.emplace(key, value.get<double>());
            }
        }
    }

    if (const auto iter = json.find("tags"); iter != json.end()) {
        if (!iter->is_array()) {
            throw std::invalid_argument("external event tags must be an array");
        }
        for (const auto& tag : *iter) {
            if (tag.is_string()) {
                event.tags.push_back(tag.get<std::string>());
            }
        }
    }

    if (const auto iter = json.find("links"); iter != json.end()) {
        if (!iter->is_array()) {
            throw std::invalid_argument("external event links must be an array");
        }
        for (const auto& link_json : *iter) {
            if (!link_json.is_object()) {
                continue;
            }
            event.links.push_back(ExternalLink{
                json_string_or(link_json, "from", event.id),
                json_string_or(link_json, "to", ""),
                json_string_or(link_json, "relation", "related_to"),
                json_double_or(link_json, "weight", 1.0)
            });
        }
    }

    return event;
}

}  // namespace pointerverse
