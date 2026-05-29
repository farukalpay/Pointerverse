// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pv/breakpoint/breakpoint_finder.hpp"
#include "pv/hash/canonical.hpp"
#include "pv/intervention/intervention_lattice.hpp"
#include "pv/intervention/intervention_trace.hpp"
#include "pv/intervention/operator_family.hpp"
#include "pv/law/verifier.hpp"

namespace pv {

class ProjectionStore;
class Repository;

struct InterventionSearchOptions {
    std::uint8_t max_depth{2};
    std::uint8_t max_composition{2};
    bool include_identity{true};
};

struct InterventionSearchSample {
    ScaleValue scale{ScaleValue::zero()};
    InterventionProgram program;
    bool replayed{false};
    bool transformed{false};
    bool survives{true};
    std::optional<Breakpoint> survivor;
    std::vector<std::string> evidence_ids;
    std::string explanation;
};

struct InterventionSurvivalRegion {
    ScaleValue birth_scale{ScaleValue::zero()};
    ScaleValue death_scale{ScaleValue::zero()};
    ScaleValue last_surviving_scale{ScaleValue::zero()};
    double persistence_length{0.0};
    bool survives_to_max_scale{false};
};

struct InterventionSearchResult {
    Hash256 search_id;
    Breakpoint breakpoint;
    std::vector<OperatorFamily> families;
    std::vector<InterventionSearchSample> samples;
    std::vector<InterventionSurvivalRegion> surviving_regions;
    std::optional<ScaleValue> birth_scale;
    std::optional<ScaleValue> death_scale;
    double persistence_length{0.0};
    std::optional<ScaleValue> minimal_killing_scale;
    std::vector<std::string> carried_evidence_ids;
    std::optional<InterventionProgram> minimal_killing_program;
    std::size_t programs_tested{0};
    std::size_t replayed_branches{0};
};

struct InterventionCompositionResult {
    InterventionProgram left;
    InterventionProgram right;
    InterventionProgram composed;
    InterventionOrder order{InterventionOrder::Incomparable};
    bool compatible{false};
    bool conflicting{false};
    bool redundant{false};
    bool order_sensitive{false};
    InterventionTrace trace;
    std::string explanation;
};

[[nodiscard]] Hash256 intervention_search_id(
    const Repository& repository,
    std::string_view branch,
    const Breakpoint& breakpoint,
    const std::vector<OperatorFamily>& families,
    InterventionSearchOptions options,
    BreakpointFindOptions find_options);

class InterventionSearch {
public:
    [[nodiscard]] InterventionSearchResult search(
        const Repository& repository,
        const ProjectionStore& store,
        std::string_view branch,
        const Breakpoint& breakpoint,
        const Verifier* verifier = nullptr,
        BreakpointFindOptions find_options = {},
        InterventionSearchOptions options = {}) const;

    [[nodiscard]] InterventionTrace trace(
        const Repository& repository,
        const ProjectionStore& store,
        std::string_view branch,
        const Breakpoint& breakpoint,
        const InterventionProgram& program,
        const Verifier* verifier = nullptr,
        BreakpointFindOptions find_options = {},
        InterventionSearchOptions options = {}) const;

    [[nodiscard]] InterventionCompositionResult compose_pair(
        const Repository& repository,
        const ProjectionStore& store,
        std::string_view branch,
        const Breakpoint& breakpoint,
        const InterventionProgram& left,
        const InterventionProgram& right,
        const Verifier* verifier = nullptr,
        BreakpointFindOptions find_options = {},
        InterventionSearchOptions options = {}) const;
};

[[nodiscard]] std::string render_intervention_families_text(
    const std::vector<OperatorFamily>& families);
[[nodiscard]] std::string render_intervention_refinement_text(
    const std::vector<OperatorFamily>& families,
    std::uint8_t depth);
[[nodiscard]] std::string render_intervention_search_text(const InterventionSearchResult& result);
[[nodiscard]] std::string render_intervention_lattice_text(const InterventionSearchResult& result);
[[nodiscard]] std::string render_intervention_composition_text(const InterventionCompositionResult& result);

}  // namespace pv
