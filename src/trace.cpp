#include "pointerverse/trace.hpp"

#include <algorithm>

namespace pointerverse {

nlohmann::json LawResult::to_json() const {
    return {
        {"name", name},
        {"passed", passed},
        {"value", value},
        {"detail", detail}
    };
}

nlohmann::json ObjectSnapshot::to_json() const {
    return {
        {"name", name},
        {"type", type},
        {"dimension", dimension},
        {"norm", norm},
        {"normalization_error", normalization_error},
        {"entropy", entropy},
        {"incoming_count", incoming_count},
        {"outgoing_count", outgoing_count}
    };
}

nlohmann::json WorldSnapshot::to_json() const {
    nlohmann::json object_json = nlohmann::json::array();
    for (const auto& object : objects) {
        object_json.push_back(object.to_json());
    }

    return {
        {"time", time},
        {"object_count", object_count},
        {"relation_count", relation_count},
        {"morphism_count", morphism_count},
        {"law_count", law_count},
        {"region_count", region_count},
        {"attractor_count", attractor_count},
        {"persistent_constraint_count", persistent_constraint_count},
        {"invalid_relation_count", invalid_relation_count},
        {"negative_causal_weight_count", negative_causal_weight_count},
        {"total_relation_weight", total_relation_weight},
        {"total_probability_mass", total_probability_mass},
        {"max_normalization_error", max_normalization_error},
        {"graph_entropy", graph_entropy},
        {"total_pressure", total_pressure},
        {"max_pressure", max_pressure},
        {"average_region_stability", average_region_stability},
        {"objects", std::move(object_json)}
    };
}

nlohmann::json TraceEvent::to_json() const {
    return {
        {"step", step},
        {"type", type},
        {"involved_objects", involved_objects},
        {"involved_links", involved_links},
        {"affected_laws", affected_laws},
        {"morphism", morphism},
        {"region", region},
        {"pressure_delta", pressure_delta},
        {"law_residuals", law_residuals},
        {"counterfactual_delta", counterfactual_delta},
        {"detail", detail}
    };
}

bool TraceStep::passed() const {
    return std::all_of(law_results.begin(), law_results.end(), [](const LawResult& result) {
        return result.passed;
    });
}

nlohmann::json TraceStep::to_json() const {
    nlohmann::json laws = nlohmann::json::array();
    for (const auto& result : law_results) {
        laws.push_back(result.to_json());
    }

    nlohmann::json structured = nlohmann::json::array();
    for (const auto& event : structured_events) {
        structured.push_back(event.to_json());
    }

    return {
        {"step", step},
        {"rewrite", rewrite},
        {"passed", passed()},
        {"before", before.to_json()},
        {"after", after.to_json()},
        {"law_results", std::move(laws)},
        {"events", events},
        {"structured_events", std::move(structured)}
    };
}

void Trace::append(TraceStep step) {
    steps_.push_back(std::move(step));
}

bool Trace::empty() const noexcept {
    return steps_.empty();
}

std::size_t Trace::size() const noexcept {
    return steps_.size();
}

const std::vector<TraceStep>& Trace::steps() const noexcept {
    return steps_;
}

nlohmann::json Trace::to_json() const {
    nlohmann::json steps = nlohmann::json::array();
    for (const auto& step : steps_) {
        steps.push_back(step.to_json());
    }
    return {
        {"version", 1},
        {"steps", std::move(steps)}
    };
}

}  // namespace pointerverse
