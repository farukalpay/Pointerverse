#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "pointerverse/types.hpp"

namespace pointerverse {

struct PressureWeights {
    double law_residual{0.45};
    double graph_entropy{0.20};
    double state_drift{0.25};
    double noncommutativity{0.10};
};

struct StructuralPressure {
    double magnitude{0.0};
    double persistence{0.0};
    double locality{0.0};
    double recurrence{0.0};
    double instability{0.0};

    [[nodiscard]] bool active(double threshold = 1e-9) const noexcept;
    [[nodiscard]] nlohmann::json to_json() const;
};

struct RegionBoundary {
    std::size_t internal_links{0};
    std::size_t external_links{0};
    double internal_density{0.0};
    double boundary_density{0.0};
    double permeability{0.0};

    [[nodiscard]] nlohmann::json to_json() const;
};

struct Region {
    RegionId id;
    std::string name;
    std::vector<ObjectHandle> objects;
    std::vector<RelationId> internal_links;
    RegionBoundary boundary;
    StructuralPressure pressure;
    double stability{1.0};
    std::string origin;

    [[nodiscard]] nlohmann::json to_json() const;
};

struct ExternalLink {
    std::string from;
    std::string to;
    std::string relation{"related_to"};
    double weight{1.0};
};

struct ExternalEvent {
    std::string id;
    std::string kind;
    double weight{1.0};
    std::map<std::string, double> metrics;
    std::vector<std::string> tags;
    std::vector<ExternalLink> links;
};

struct SymbolicCompression {
    std::string symbol_name;
    std::string object_type;
    std::string relation;
    std::string interpretation;
    StructuralPressure pressure;
    bool creates_constraint{false};
    bool creates_attractor_candidate{false};
    bool creates_gap{false};

    [[nodiscard]] nlohmann::json to_json() const;
};

struct InternalizationResult {
    std::vector<ObjectHandle> objects;
    std::vector<RelationId> pointers;
    StructuralPressure pressure;
    std::vector<std::string> events;

    [[nodiscard]] nlohmann::json to_json() const;
};

struct MorphismApplicationResult {
    bool applied{false};
    bool valid{false};
    std::string morphism;
    std::string target;
    std::string reason;
    double pressure_delta{0.0};
    double law_residual{0.0};
    double counterfactual_delta{0.0};
    std::vector<std::string> events;

    [[nodiscard]] nlohmann::json to_json() const;
};

[[nodiscard]] StructuralPressure compute_pressure(
    double law_residual,
    double graph_entropy,
    double state_drift,
    double noncommutativity,
    const PressureWeights& weights = {});

[[nodiscard]] SymbolicCompression compress_external_event(const ExternalEvent& event);
[[nodiscard]] ExternalEvent external_event_from_json(const nlohmann::json& json);

}  // namespace pointerverse
