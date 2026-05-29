// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <utility>

#include "pv/breakpoint/breakpoint_finder.hpp"
#include "pv/breakpoint/repair_candidate.hpp"
#include "pv/ingest/graph_log_importer.hpp"
#include "pv/ingest/ingestion_index.hpp"
#include "pv/measure/counterfactual_measure.hpp"
#include "pv/projection/projection_store.hpp"
#include "pv/storage/repository.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_counterfactual_repo() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_counterfactual_test_" + std::to_string(stamp));
    std::filesystem::remove_all(path);
    return path;
}

GraphEvent graph_event(std::string id) {
    GraphEvent event;
    event.id = std::move(id);
    event.source = "external";
    event.from = "A";
    event.from_type = "Entity";
    event.to = "B";
    event.to_type = "Entity";
    event.relation = "causes";
    return event;
}

Breakpoint repeated_breakpoint(Repository& repository) {
    IngestionIndex index{repository.root()};
    IngestionOptions options;
    options.branch = "main";
    REQUIRE(GraphLogImporter{repository}.import({graph_event("e1"), graph_event("e2")}, index, options).accepted == 2);

    ProjectionStore store{repository};
    const auto breakpoints = BreakpointFinder{}.find(store, "main");
    const auto repeated = std::ranges::find_if(breakpoints, [](const Breakpoint& breakpoint) {
        return breakpoint.kind == BreakpointKind::RepeatedRelation;
    });
    REQUIRE(repeated != breakpoints.end());
    return *repeated;
}

const RepairCandidate& candidate_for(const std::vector<RepairCandidate>& candidates, RepairAction action) {
    const auto iter = std::ranges::find_if(candidates, [&](const RepairCandidate& candidate) {
        return candidate.action == action;
    });
    REQUIRE(iter != candidates.end());
    return *iter;
}

}  // namespace

TEST_CASE("counterfactual replay determines breakpoint survival") {
    const auto root = temp_counterfactual_repo();
    auto repository = Repository::init(root);
    const auto breakpoint = repeated_breakpoint(repository);

    ProjectionStore store{repository};
    const auto candidates = RepairCandidateBuilder{}.build_all(store, "main", breakpoint);
    REQUIRE(candidates.size() == 4);

    const auto removed = CounterfactualMeasure{}.evaluate(
        repository,
        "main",
        breakpoint,
        candidate_for(candidates, RepairAction::RemoveTriggeringRelation));
    REQUIRE(removed.replayed);
    REQUIRE_FALSE(removed.survives);
    REQUIRE(removed.canonical_cost > 0);

    const auto constrained = CounterfactualMeasure{}.evaluate(
        repository,
        "main",
        breakpoint,
        candidate_for(candidates, RepairAction::ConstrainTriggeringRelation));
    REQUIRE(constrained.replayed);
    REQUIRE(constrained.survives);

    std::filesystem::remove_all(root);
}

TEST_CASE("counterfactual filtration records breakpoint persistence across intervention scales") {
    const auto root = temp_counterfactual_repo();
    auto repository = Repository::init(root);
    const auto breakpoint = repeated_breakpoint(repository);

    ProjectionStore store{repository};
    const auto filtration = CounterfactualMeasure{}.filtration(repository, store, "main", breakpoint);

    REQUIRE(filtration.samples.size() == 5);
    REQUIRE(filtration.birth_scale.has_value());
    REQUIRE(*filtration.birth_scale == 0.0);
    REQUIRE(filtration.minimal_killing_scale.has_value());
    REQUIRE(*filtration.minimal_killing_scale > 0.0);
    REQUIRE(*filtration.minimal_killing_scale <= 1.0);
    REQUIRE(filtration.death_scale == filtration.minimal_killing_scale);
    REQUIRE_FALSE(filtration.surviving_regions.empty());
    REQUIRE(filtration.surviving_regions.front().birth_scale == 0.0);
    REQUIRE(filtration.persistence_length > 0.0);

    const auto identity = std::ranges::find_if(filtration.samples, [](const CounterfactualFiltrationSample& sample) {
        return sample.intervention == CounterfactualInterventionKind::Identity;
    });
    REQUIRE(identity != filtration.samples.end());
    REQUIRE(identity->replayed);
    REQUIRE_FALSE(identity->transformed);
    REQUIRE(identity->survives);

    const auto killed = std::ranges::find_if(filtration.samples, [](const CounterfactualFiltrationSample& sample) {
        return !sample.survives;
    });
    REQUIRE(killed != filtration.samples.end());
    REQUIRE(killed->scale == *filtration.minimal_killing_scale);

    REQUIRE(std::ranges::find(filtration.carried_evidence_ids, "external/e2") != filtration.carried_evidence_ids.end());

    std::filesystem::remove_all(root);
}

TEST_CASE("breakpoint survival requires same kind relation set and entity overlap") {
    Breakpoint original;
    original.kind = BreakpointKind::RepeatedRelation;
    original.affected_relations = {"causes"};
    original.affected_entities = {"A", "B"};

    auto equivalent = original;
    equivalent.affected_entities = {"B", "C"};
    REQUIRE(equivalent_breakpoint(original, equivalent));

    auto replaced = equivalent;
    replaced.affected_relations = {"blocks"};
    REQUIRE_FALSE(equivalent_breakpoint(original, replaced));
}
