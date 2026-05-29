// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pv/breakpoint/breakpoint.hpp"
#include "pv/breakpoint/breakpoint_finder.hpp"
#include "pv/breakpoint/repair_candidate.hpp"
#include "pv/law/verifier.hpp"

namespace pv {

class ProjectionStore;
class Repository;

struct CounterfactualRepairMeasure {
    RepairCandidate candidate;
    std::uint64_t canonical_cost{0};
    std::string canonical_script;
    bool replayed{false};
    bool survives{true};
    std::optional<Breakpoint> survivor;
    std::string explanation;
};

enum class CounterfactualInterventionKind {
    Identity,
    ConstrainTriggeringRelation,
    DelayTriggeringRelation,
    ReplaceTriggeringRelation,
    RemoveTriggeringRelation
};

struct CounterfactualFiltrationOptions {
    std::size_t intervals{4};
    std::vector<double> scales;
};

struct CounterfactualFiltrationSample {
    double scale{0.0};
    CounterfactualInterventionKind intervention{CounterfactualInterventionKind::Identity};
    bool replayed{false};
    bool transformed{false};
    bool survives{true};
    std::optional<RepairCandidate> candidate;
    std::optional<CounterfactualRepairMeasure> repair;
    std::optional<Breakpoint> survivor;
    std::vector<std::string> evidence_ids;
    std::string explanation;
};

struct CounterfactualSurvivalRegion {
    double birth_scale{0.0};
    double death_scale{0.0};
    double last_surviving_scale{0.0};
    double persistence_length{0.0};
    bool survives_to_max_scale{false};
};

struct CounterfactualFiltration {
    Breakpoint breakpoint;
    std::vector<CounterfactualFiltrationSample> samples;
    std::optional<double> birth_scale;
    std::optional<double> death_scale;
    double persistence_length{0.0};
    std::vector<CounterfactualSurvivalRegion> surviving_regions;
    std::optional<double> minimal_killing_scale;
    std::vector<std::string> carried_evidence_ids;
};

class CounterfactualMeasure {
public:
    [[nodiscard]] CounterfactualRepairMeasure evaluate(
        const Repository& repository,
        std::string_view branch,
        const Breakpoint& breakpoint,
        const RepairCandidate& candidate,
        const Verifier* verifier = nullptr,
        BreakpointFindOptions find_options = {}) const;

    [[nodiscard]] CounterfactualFiltration filtration(
        const Repository& repository,
        const ProjectionStore& store,
        std::string_view branch,
        const Breakpoint& breakpoint,
        const Verifier* verifier = nullptr,
        BreakpointFindOptions find_options = {},
        CounterfactualFiltrationOptions options = {}) const;
};

[[nodiscard]] std::string_view to_string(CounterfactualInterventionKind kind) noexcept;

[[nodiscard]] bool equivalent_breakpoint(
    const Breakpoint& original,
    const Breakpoint& candidate);

[[nodiscard]] std::optional<Breakpoint> surviving_breakpoint(
    const Breakpoint& original,
    const std::vector<Breakpoint>& repaired);

}  // namespace pv
