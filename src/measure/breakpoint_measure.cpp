// SPDX-License-Identifier: Apache-2.0
#include "pv/measure/breakpoint_measure.hpp"

#include <algorithm>
#include <exception>
#include <iomanip>
#include <limits>
#include <sstream>

#include <fmt/format.h>

#include "pv/breakpoint/evidence_chain.hpp"
#include "pv/intervention/intervention_search.hpp"
#include "pv/kernel/canonical_codec.hpp"
#include "pv/measure/intrinsic_edit_cost.hpp"
#include "pv/projection/projection_store.hpp"

namespace pv {
namespace {

std::string short_commit(CommitId id) {
    return to_hex(id.value).substr(0, 12);
}

void write_step(CanonicalWriter& writer, const EvidenceChainStep& step) {
    writer.hash(step.commit.value);
    writer.u64(step.epoch.value);
    writer.string(step.event);
    writer.string(step.detail);
    writer.string(step.evidence_id);
}

std::uint64_t canonical_size(const EvidenceChain& chain) {
    CanonicalWriter writer;
    writer.string("EvidenceChain:v1");
    writer.string(chain.breakpoint.id);
    writer.string(std::string{to_string(chain.breakpoint.kind)});
    write_step(writer, chain.triggering_event);
    writer.u64(chain.prior_enabling_events.size());
    for (const auto& step : chain.prior_enabling_events) {
        write_step(writer, step);
    }
    writer.u64(chain.affected_entities.size());
    for (const auto& entity : chain.affected_entities) {
        writer.string(entity);
    }
    writer.u64(chain.affected_relations.size());
    for (const auto& relation : chain.affected_relations) {
        writer.string(relation);
    }
    writer.u64(chain.evidence_ids.size());
    for (const auto& evidence : chain.evidence_ids) {
        writer.string(evidence);
    }
    return static_cast<std::uint64_t>(writer.bytes().size());
}

std::uint64_t canonical_history_prefix_size(
    const ProjectionStore& store,
    std::string_view branch,
    const Breakpoint& breakpoint) {
    CanonicalWriter writer;
    writer.string("BreakpointHistoryPrefix:v1");
    for (const auto& record : store.history(branch)) {
        writer.hash(record.id.value);
        writer.u64(record.after_epoch.value);
        writer.string(record.label);
        writer.u64(record.events.size());
        for (const auto& event : record.events) {
            if (record.id == breakpoint.trigger.commit && event.epoch.value > breakpoint.trigger.epoch.value) {
                continue;
            }
            encode(writer, event);
        }
        if (record.id == breakpoint.trigger.commit) {
            break;
        }
    }
    return std::max<std::uint64_t>(1, static_cast<std::uint64_t>(writer.bytes().size()));
}

double evidence_compression(
    const ProjectionStore& store,
    std::string_view branch,
    const Breakpoint& breakpoint,
    const EvidenceChain& chain) {
    const auto evidence = canonical_size(chain);
    const auto prefix = canonical_history_prefix_size(store, branch, breakpoint);
    return static_cast<double>(evidence) / static_cast<double>(prefix);
}

bool better_rank(const BreakpointMeasurement& left, const BreakpointMeasurement& right) {
    if (left.has_eliminating_repair != right.has_eliminating_repair) {
        return left.has_eliminating_repair;
    }
    if (left.severity != right.severity) {
        return left.severity > right.severity;
    }
    if (left.compression != right.compression) {
        return left.compression < right.compression;
    }
    return left.breakpoint.id < right.breakpoint.id;
}

std::string severity_text(const BreakpointMeasurement& measurement) {
    if (!measurement.has_eliminating_repair) {
        return "unresolved";
    }
    return std::to_string(measurement.severity);
}

std::string scale_text(const std::optional<double>& scale) {
    if (!scale.has_value()) {
        return "none";
    }
    return fmt::format("{:.6f}", *scale);
}

std::string evidence_text(const std::vector<std::string>& evidence) {
    if (evidence.empty()) {
        return "none";
    }
    std::ostringstream output;
    for (std::size_t index = 0; index < evidence.size(); ++index) {
        if (index != 0) {
            output << ", ";
        }
        output << evidence[index];
    }
    return output.str();
}

}  // namespace

BreakpointMeasurement BreakpointMeasure::measure(
    const Repository& repository,
    const ProjectionStore& store,
    std::string_view branch,
    const Breakpoint& breakpoint,
    const Verifier* verifier,
    BreakpointFindOptions find_options) const {
    BreakpointMeasurement measurement;
    measurement.breakpoint = breakpoint;
    const auto chain = EvidenceChainBuilder{}.build(store, branch, breakpoint);
    measurement.evidence_chain_ids = chain.evidence_ids;
    measurement.compression = evidence_compression(store, branch, breakpoint, chain);

    measurement.filtration.breakpoint = breakpoint;
    try {
        measurement.intervention_search = InterventionSearch{}.search(
            repository,
            store,
            branch,
            breakpoint,
            verifier,
            find_options);
        measurement.filtration = CounterfactualMeasure{}.filtration(
            repository,
            store,
            branch,
            breakpoint,
            verifier,
            find_options);
    } catch (const std::exception&) {
        return measurement;
    }

    if (measurement.intervention_search.minimal_killing_program.has_value()) {
        measurement.has_eliminating_repair = true;
        measurement.severity = measurement.intervention_search.minimal_killing_program->canonical_cost;
    }

    for (const auto& sample : measurement.filtration.samples) {
        if (!sample.repair.has_value()) {
            continue;
        }
        auto repaired = *sample.repair;
        if (repaired.replayed && !repaired.survives) {
            measurement.has_eliminating_repair = true;
            measurement.severity = std::min(measurement.severity, repaired.canonical_cost);
        }
        measurement.repairs.push_back(std::move(repaired));
    }
    return measurement;
}

std::vector<BreakpointMeasurement> BreakpointMeasure::rank(
    const Repository& repository,
    const ProjectionStore& store,
    std::string_view branch,
    const Verifier* verifier,
    BreakpointFindOptions find_options) const {
    const auto breakpoints = BreakpointFinder{}.find(store, branch, find_options);
    std::vector<BreakpointMeasurement> measurements;
    measurements.reserve(breakpoints.size());
    for (const auto& breakpoint : breakpoints) {
        measurements.push_back(measure(repository, store, branch, breakpoint, verifier, find_options));
    }
    std::ranges::sort(measurements, better_rank);
    return measurements;
}

std::uint64_t edge_criticality(
    std::string_view evidence_edge,
    const std::vector<BreakpointMeasurement>& measurements) {
    std::uint64_t out = std::numeric_limits<std::uint64_t>::max();
    for (const auto& measurement : measurements) {
        if (!measurement.has_eliminating_repair) {
            continue;
        }
        const auto contains_edge = std::ranges::any_of(measurement.evidence_chain_ids, [&](const auto& evidence) {
            return evidence == evidence_edge;
        });
        if (contains_edge) {
            out = std::min(out, measurement.severity);
        }
    }
    return out;
}

std::string render_breakpoint_measure_text(const BreakpointMeasurement& measurement) {
    std::ostringstream output;
    output << fmt::format("Breakpoint measure: {}\n", measurement.breakpoint.id);
    output << "-------------------\n";
    output << fmt::format("kind:        {}\n", to_string(measurement.breakpoint.kind));
    output << fmt::format("branch:      {}\n", measurement.breakpoint.branch);
    output << fmt::format("trigger:     epoch {} commit {} {}\n",
        measurement.breakpoint.trigger.epoch.value,
        short_commit(measurement.breakpoint.trigger.commit),
        measurement.breakpoint.trigger.detail);
    output << fmt::format("severity:    {}\n", severity_text(measurement));
    output << fmt::format("compression: {:.6f}\n", measurement.compression);
    if (!empty(measurement.intervention_search.search_id)) {
        output << fmt::format("search id:   {}\n", to_hex(measurement.intervention_search.search_id).substr(0, 12));
    }
    output << fmt::format(
        "filtration:  birth={} death={} persistence={:.6f} kill={}\n",
        scale_text(measurement.filtration.birth_scale),
        scale_text(measurement.filtration.death_scale),
        measurement.filtration.persistence_length,
        scale_text(measurement.filtration.minimal_killing_scale));
    output << fmt::format("evidence:    {}\n", evidence_text(measurement.filtration.carried_evidence_ids));
    output << "surviving regions:\n";
    if (measurement.filtration.surviving_regions.empty()) {
        output << "  none\n";
    }
    for (const auto& region : measurement.filtration.surviving_regions) {
        output << fmt::format(
            "  [{:.6f}, {:.6f}{} last={:.6f} persistence={:.6f}\n",
            region.birth_scale,
            region.death_scale,
            region.survives_to_max_scale ? "]" : ")",
            region.last_surviving_scale,
            region.persistence_length);
    }
    output << "repairs:\n";
    for (const auto& repair : measurement.repairs) {
        output << fmt::format(
            "  {} cost={} replay={} survives={} {}\n",
            to_string(repair.candidate.action),
            repair.canonical_cost,
            repair.replayed ? "yes" : "no",
            repair.survives ? "yes" : "no",
            repair.explanation);
    }
    return output.str();
}

std::string render_breakpoint_rank_text(
    std::string_view branch,
    const std::vector<BreakpointMeasurement>& measurements) {
    std::ostringstream output;
    output << fmt::format("Breakpoint intervention-cost rank: {}\n", branch);
    output << "----------------------------------\n";
    if (measurements.empty()) {
        output << "none\n";
        return output.str();
    }
    for (const auto& measurement : measurements) {
        output << fmt::format(
            "{} severity={} compression={:.6f} {} {}\n",
            measurement.breakpoint.id,
            severity_text(measurement),
            measurement.compression,
            to_string(measurement.breakpoint.kind),
            measurement.breakpoint.summary);
    }
    return output.str();
}

std::string render_repair_set_text(const BreakpointMeasurement& measurement) {
    std::ostringstream output;
    output << fmt::format("Breakpoint counterfactual filtration: {}\n", measurement.breakpoint.id);
    output << "------------------------------------\n";
    output << fmt::format(
        "birth={} death={} persistence={:.6f} kill={}\n",
        scale_text(measurement.filtration.birth_scale),
        scale_text(measurement.filtration.death_scale),
        measurement.filtration.persistence_length,
        scale_text(measurement.filtration.minimal_killing_scale));
    output << fmt::format("evidence carried across scales: {}\n", evidence_text(measurement.filtration.carried_evidence_ids));
    output << "scales:\n";
    for (const auto& sample : measurement.filtration.samples) {
        output << fmt::format(
            "  t={:.6f} {} replay={} transform={} survives={}",
            sample.scale,
            to_string(sample.intervention),
            sample.replayed ? "yes" : "no",
            sample.transformed ? "yes" : "no",
            sample.survives ? "yes" : "no");
        if (sample.repair.has_value()) {
            output << fmt::format(" cost={}", sample.repair->canonical_cost);
        }
        output << fmt::format(" evidence={}\n", evidence_text(sample.evidence_ids));
    }
    output << "repairs:\n";
    for (const auto& repair : measurement.repairs) {
        output << fmt::format(
            "{} cost={} survives={} replay={}\n",
            to_string(repair.candidate.action),
            repair.canonical_cost,
            repair.survives ? "yes" : "no",
            repair.replayed ? "yes" : "no");
        output << repair.candidate.script;
        if (!repair.candidate.script.empty() && repair.candidate.script.back() != '\n') {
            output << '\n';
        }
    }
    return output.str();
}

}  // namespace pv
