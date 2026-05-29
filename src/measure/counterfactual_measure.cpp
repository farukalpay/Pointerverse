// SPDX-License-Identifier: Apache-2.0
#include "pv/measure/counterfactual_measure.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>

#include <fmt/format.h>

#include "pv/intervention/intervention_search.hpp"
#include "pv/measure/intrinsic_edit_cost.hpp"
#include "pv/projection/projection_store.hpp"

namespace pv {
namespace {

bool same_relation_set(std::vector<std::string> left, std::vector<std::string> right) {
    std::ranges::sort(left);
    std::ranges::sort(right);
    return left == right;
}

bool overlapping_entities(const std::vector<std::string>& left, const std::vector<std::string>& right) {
    return std::ranges::any_of(left, [&](const auto& entity) {
        return std::ranges::find(right, entity) != right.end();
    });
}

std::vector<std::string> sorted_unique_strings(std::vector<std::string> values) {
    std::ranges::sort(values);
    values.erase(std::ranges::unique(values).begin(), values.end());
    return values;
}

std::uint8_t depth_for_intervals(std::size_t intervals) {
    intervals = std::max<std::size_t>(1, intervals);
    std::uint8_t depth = 0;
    std::size_t count = 1;
    while (count < intervals && depth < 16) {
        count <<= 1U;
        depth += 1U;
    }
    return depth;
}

ScaleValue scale_from_double(double value) {
    const auto clamped = std::clamp(value, 0.0, 1.0);
    constexpr std::uint8_t exponent = 10;
    const auto denominator = std::uint64_t{1} << exponent;
    const auto numerator = static_cast<std::uint64_t>(std::llround(clamped * static_cast<double>(denominator)));
    return ScaleValue::dyadic(numerator, exponent);
}

std::vector<ScaleValue> scale_samples(CounterfactualFiltrationOptions options) {
    if (options.scales.empty()) {
        return dyadic_refinement_scales(depth_for_intervals(options.intervals));
    }
    std::vector<ScaleValue> scales;
    scales.reserve(options.scales.size() + 2);
    for (const auto scale : options.scales) {
        scales.push_back(scale_from_double(scale));
    }
    scales.push_back(ScaleValue::zero());
    scales.push_back(ScaleValue::one());
    std::ranges::sort(scales);
    scales.erase(std::ranges::unique(scales).begin(), scales.end());
    return scales;
}

CounterfactualInterventionKind intervention_for_scale(ScaleValue scale) noexcept {
    if (scale == ScaleValue::zero()) {
        return CounterfactualInterventionKind::Identity;
    }
    constexpr std::array ordered{
        CounterfactualInterventionKind::ConstrainTriggeringRelation,
        CounterfactualInterventionKind::DelayTriggeringRelation,
        CounterfactualInterventionKind::ReplaceTriggeringRelation,
        CounterfactualInterventionKind::RemoveTriggeringRelation
    };
    const auto bucket = std::min<std::size_t>(
        ordered.size() - 1,
        static_cast<std::size_t>(std::ceil(scale.to_double() * static_cast<double>(ordered.size()))) - 1U);
    return ordered[bucket];
}

InterventionKind intervention_kind(CounterfactualInterventionKind kind) {
    switch (kind) {
    case CounterfactualInterventionKind::Identity:
        return InterventionKind::Identity;
    case CounterfactualInterventionKind::ConstrainTriggeringRelation:
        return InterventionKind::ConstrainTriggeringRelation;
    case CounterfactualInterventionKind::DelayTriggeringRelation:
        return InterventionKind::DelayTriggeringRelation;
    case CounterfactualInterventionKind::ReplaceTriggeringRelation:
        return InterventionKind::ReplaceTriggeringRelation;
    case CounterfactualInterventionKind::RemoveTriggeringRelation:
        return InterventionKind::RemoveTriggeringRelation;
    }
    return InterventionKind::Identity;
}

const OperatorFamily* family_for(
    const std::vector<OperatorFamily>& families,
    CounterfactualInterventionKind kind) {
    const auto target = intervention_kind(kind);
    const auto iter = std::ranges::find_if(families, [&](const OperatorFamily& family) {
        return family.kind == target;
    });
    if (iter == families.end()) {
        return nullptr;
    }
    return &*iter;
}

CounterfactualRepairMeasure repair_measure_from_trace(
    const RepairCandidate& candidate,
    const InterventionTrace& trace) {
    CounterfactualRepairMeasure result;
    result.candidate = candidate;
    const auto cost = IntrinsicEditCost{}.measure(candidate.script);
    result.canonical_cost = cost.value;
    result.canonical_script = cost.canonical_script;
    result.replayed = trace.transformed;
    result.survives = trace.survives;
    result.survivor = trace.survivor;
    result.explanation = trace.explanation;
    if (!trace.transformed) {
        result.explanation = "candidate did not edit the replayed history";
    }
    return result;
}

std::vector<std::string> sample_evidence(const std::optional<Breakpoint>& survivor) {
    if (!survivor.has_value()) {
        return {};
    }
    return sorted_unique_strings(survivor->evidence_ids);
}

std::vector<std::string> carried_evidence(const std::vector<CounterfactualFiltrationSample>& samples) {
    std::vector<std::string> carried;
    bool initialized = false;
    for (const auto& sample : samples) {
        if (!sample.survives) {
            continue;
        }
        auto evidence = sorted_unique_strings(sample.evidence_ids);
        if (!initialized) {
            carried = std::move(evidence);
            initialized = true;
            continue;
        }
        std::vector<std::string> intersection;
        std::ranges::set_intersection(carried, evidence, std::back_inserter(intersection));
        carried = std::move(intersection);
    }
    return carried;
}

void summarize_filtration(CounterfactualFiltration& filtration) {
    if (filtration.samples.empty()) {
        return;
    }

    const auto max_scale = filtration.samples.back().scale;
    bool in_region = false;
    CounterfactualSurvivalRegion region;
    for (const auto& sample : filtration.samples) {
        if (!sample.survives) {
            if (!filtration.minimal_killing_scale.has_value()) {
                filtration.minimal_killing_scale = sample.scale;
            }
            if (in_region) {
                region.death_scale = sample.scale;
                region.persistence_length = std::max(0.0, region.death_scale - region.birth_scale);
                filtration.persistence_length += region.persistence_length;
                if (!filtration.death_scale.has_value()) {
                    filtration.death_scale = region.death_scale;
                }
                filtration.surviving_regions.push_back(region);
                region = {};
                in_region = false;
            }
            continue;
        }

        if (!filtration.birth_scale.has_value()) {
            filtration.birth_scale = sample.scale;
        }
        if (!in_region) {
            region = {};
            region.birth_scale = sample.scale;
            in_region = true;
        }
        region.last_surviving_scale = sample.scale;
    }

    if (in_region) {
        region.death_scale = max_scale;
        region.survives_to_max_scale = true;
        region.persistence_length = std::max(0.0, region.death_scale - region.birth_scale);
        filtration.persistence_length += region.persistence_length;
        filtration.surviving_regions.push_back(region);
    }

    filtration.carried_evidence_ids = carried_evidence(filtration.samples);
}

}  // namespace

std::string_view to_string(CounterfactualInterventionKind kind) noexcept {
    switch (kind) {
    case CounterfactualInterventionKind::Identity:
        return "identity";
    case CounterfactualInterventionKind::ConstrainTriggeringRelation:
        return "constrain_triggering_relation";
    case CounterfactualInterventionKind::DelayTriggeringRelation:
        return "delay_triggering_relation";
    case CounterfactualInterventionKind::ReplaceTriggeringRelation:
        return "replace_triggering_relation";
    case CounterfactualInterventionKind::RemoveTriggeringRelation:
        return "remove_triggering_relation";
    }
    return "identity";
}

bool equivalent_breakpoint(const Breakpoint& original, const Breakpoint& candidate) {
    return original.kind == candidate.kind
        && same_relation_set(original.affected_relations, candidate.affected_relations)
        && overlapping_entities(original.affected_entities, candidate.affected_entities);
}

std::optional<Breakpoint> surviving_breakpoint(
    const Breakpoint& original,
    const std::vector<Breakpoint>& repaired) {
    const auto iter = std::ranges::find_if(repaired, [&](const Breakpoint& candidate) {
        return equivalent_breakpoint(original, candidate);
    });
    if (iter == repaired.end()) {
        return std::nullopt;
    }
    return *iter;
}

CounterfactualRepairMeasure CounterfactualMeasure::evaluate(
    const Repository& repository,
    std::string_view branch,
    const Breakpoint& breakpoint,
    const RepairCandidate& candidate,
    const Verifier* verifier,
    BreakpointFindOptions find_options) const {
    const auto op = intervention_operator_from_repair(candidate, ScaleValue::one());
    const auto program = make_intervention_program({op});
    ProjectionStore store{repository};
    InterventionSearchOptions options;
    options.max_depth = 0;
    options.max_composition = 1;
    try {
        const auto trace = InterventionSearch{}.trace(
            repository,
            store,
            branch,
            breakpoint,
            program,
            verifier,
            find_options,
            options);
        return repair_measure_from_trace(candidate, trace);
    } catch (const std::exception& error) {
        auto failed = repair_measure_from_trace(candidate, InterventionTrace{});
        failed.replayed = false;
        failed.survives = true;
        failed.explanation = fmt::format("counterfactual replay failed: {}", error.what());
        return failed;
    }
}

CounterfactualFiltration CounterfactualMeasure::filtration(
    const Repository& repository,
    const ProjectionStore& store,
    std::string_view branch,
    const Breakpoint& breakpoint,
    const Verifier* verifier,
    BreakpointFindOptions find_options,
    CounterfactualFiltrationOptions options) const {
    CounterfactualFiltration result;
    result.breakpoint = breakpoint;

    const auto families = OperatorFamilyBuilder{}.build(store, branch, breakpoint);
    InterventionSearchOptions search_options;
    search_options.max_depth = depth_for_intervals(options.intervals);
    search_options.max_composition = 1;

    for (const auto scale : scale_samples(std::move(options))) {
        CounterfactualFiltrationSample sample;
        sample.scale = scale.to_double();
        sample.intervention = intervention_for_scale(scale);

        InterventionProgram program = identity_intervention_program();
        const OperatorFamily* family = nullptr;
        if (sample.intervention != CounterfactualInterventionKind::Identity) {
            family = family_for(families, sample.intervention);
            if (family == nullptr) {
                sample.replayed = false;
                sample.transformed = false;
                sample.survives = true;
                sample.explanation = "no operator family available for scale";
                result.samples.push_back(std::move(sample));
                continue;
            }
            const auto op = make_operator(*family, scale);
            program = make_intervention_program({op});
            sample.candidate = repair_candidate_from_operator(op);
        }

        try {
            const auto trace = InterventionSearch{}.trace(
                repository,
                store,
                branch,
                breakpoint,
                program,
                verifier,
                find_options,
                search_options);
            sample.replayed = trace.replayed;
            sample.transformed = trace.transformed;
            sample.survives = trace.survives;
            sample.survivor = trace.survivor;
            sample.evidence_ids = sample_evidence(sample.survivor);
            sample.explanation = trace.explanation;
            if (sample.candidate.has_value()) {
                sample.repair = repair_measure_from_trace(*sample.candidate, trace);
            }
        } catch (const std::exception& error) {
            sample.replayed = false;
            sample.transformed = false;
            sample.survives = true;
            sample.explanation = fmt::format("counterfactual replay failed: {}", error.what());
        }
        result.samples.push_back(std::move(sample));
    }

    summarize_filtration(result);
    return result;
}

}  // namespace pv
