// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <utility>

#include "pv/breakpoint/breakpoint_finder.hpp"
#include "pv/ingest/graph_log_importer.hpp"
#include "pv/ingest/ingestion_index.hpp"
#include "pv/intervention/intervention_search.hpp"
#include "pv/measure/breakpoint_measure.hpp"
#include "pv/projection/projection_store.hpp"
#include "pv/storage/repository.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_intervention_repo() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_intervention_test_" + std::to_string(stamp));
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

Breakpoint seed_repeated(Repository& repository) {
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

}  // namespace

TEST_CASE("intervention search deterministically finds a minimal killing program") {
    const auto root = temp_intervention_repo();
    auto repository = Repository::init(root);
    const auto breakpoint = seed_repeated(repository);
    ProjectionStore store{repository};

    InterventionSearchOptions options;
    options.max_depth = 1;
    options.max_composition = 2;
    const auto first = InterventionSearch{}.search(repository, store, "main", breakpoint, nullptr, {}, options);
    const auto second = InterventionSearch{}.search(repository, store, "main", breakpoint, nullptr, {}, options);

    REQUIRE(first.search_id == second.search_id);
    REQUIRE(first.programs_tested == second.programs_tested);
    REQUIRE(first.minimal_killing_program.has_value());
    REQUIRE(first.minimal_killing_program->canonical_cost == second.minimal_killing_program->canonical_cost);
    REQUIRE(first.replayed_branches > 0);
    REQUIRE(std::ranges::find(first.carried_evidence_ids, "external/e2") != first.carried_evidence_ids.end());

    const auto measurement = BreakpointMeasure{}.measure(repository, store, "main", breakpoint);
    REQUIRE(measurement.has_eliminating_repair);
    REQUIRE(measurement.severity == first.minimal_killing_program->canonical_cost);

    std::filesystem::remove_all(root);
}
