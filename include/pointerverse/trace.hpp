#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace pointerverse {

struct LawResult {
    std::string name;
    bool passed{false};
    double value{0.0};
    std::string detail;

    [[nodiscard]] nlohmann::json to_json() const;
};

struct ObjectSnapshot {
    std::string name;
    std::string type;
    std::size_t dimension{0};
    double norm{0.0};
    double normalization_error{0.0};
    double entropy{0.0};
    std::size_t incoming_count{0};
    std::size_t outgoing_count{0};

    [[nodiscard]] nlohmann::json to_json() const;
};

struct WorldSnapshot {
    std::uint64_t time{0};
    std::size_t object_count{0};
    std::size_t relation_count{0};
    std::size_t morphism_count{0};
    std::size_t law_count{0};
    std::size_t region_count{0};
    std::size_t attractor_count{0};
    std::size_t persistent_constraint_count{0};
    std::size_t invalid_relation_count{0};
    std::size_t negative_causal_weight_count{0};
    double total_relation_weight{0.0};
    double total_probability_mass{0.0};
    double max_normalization_error{0.0};
    double graph_entropy{0.0};
    double total_pressure{0.0};
    double max_pressure{0.0};
    double average_region_stability{1.0};
    std::vector<ObjectSnapshot> objects;

    [[nodiscard]] nlohmann::json to_json() const;
};

struct TraceEvent {
    std::uint64_t step{0};
    std::string type;
    std::vector<std::string> involved_objects;
    std::vector<std::string> involved_links;
    std::vector<std::string> affected_laws;
    std::string morphism;
    std::string region;
    double pressure_delta{0.0};
    std::map<std::string, double> law_residuals;
    double counterfactual_delta{0.0};
    std::string detail;

    [[nodiscard]] nlohmann::json to_json() const;
};

struct TraceStep {
    std::uint64_t step{0};
    std::string rewrite;
    WorldSnapshot before;
    WorldSnapshot after;
    std::vector<LawResult> law_results;
    std::vector<std::string> events;
    std::vector<TraceEvent> structured_events;

    [[nodiscard]] bool passed() const;
    [[nodiscard]] nlohmann::json to_json() const;
};

class Trace {
public:
    void append(TraceStep step);

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] const std::vector<TraceStep>& steps() const noexcept;
    [[nodiscard]] nlohmann::json to_json() const;

private:
    std::vector<TraceStep> steps_;
};

}  // namespace pointerverse
