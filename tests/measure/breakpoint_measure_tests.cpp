// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>

#include "pv/breakpoint/breakpoint_finder.hpp"
#include "pv/ingest/graph_log_importer.hpp"
#include "pv/ingest/ingestion_index.hpp"
#include "pv/measure/breakpoint_measure.hpp"
#include "pv/projection/projection_store.hpp"
#include "pv/law/verifier.hpp"
#include "pv/runtime/transaction_types.hpp"
#include "pv/storage/repository.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_breakpoint_measure_repo() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_breakpoint_measure_" + std::to_string(stamp));
    std::filesystem::remove_all(path);
    return path;
}

GraphEvent measure_event(std::string id) {
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
    REQUIRE(GraphLogImporter{repository}.import({measure_event("e1"), measure_event("e2")}, index, options).accepted == 2);

    ProjectionStore store{repository};
    const auto breakpoints = BreakpointFinder{}.find(store, "main");
    const auto repeated = std::ranges::find_if(breakpoints, [](const Breakpoint& breakpoint) {
        return breakpoint.kind == BreakpointKind::RepeatedRelation;
    });
    REQUIRE(repeated != breakpoints.end());
    return *repeated;
}

Transaction object_tx(World& world, std::string name) {
    Transaction tx;
    tx.label = "object " + name;
    tx.delta = world.object_delta(std::move(name), "Node");
    return tx;
}

}  // namespace

TEST_CASE("breakpoint severity is minimum replayed edit cost that eliminates survival") {
    const auto root = temp_breakpoint_measure_repo();
    auto repository = Repository::init(root);
    const auto breakpoint = seed_repeated(repository);
    ProjectionStore store{repository};

    const auto first = BreakpointMeasure{}.measure(repository, store, "main", breakpoint);
    const auto second = BreakpointMeasure{}.measure(repository, store, "main", breakpoint);

    REQUIRE(first.has_eliminating_repair);
    REQUIRE(first.severity > 0);
    REQUIRE(first.severity == second.severity);
    REQUIRE(first.repairs.size() == 4);
    REQUIRE(first.compression > 0.0);

    auto minimum = std::numeric_limits<std::uint64_t>::max();
    for (const auto& repair : first.repairs) {
        if (repair.replayed && !repair.survives) {
            minimum = std::min(minimum, repair.canonical_cost);
        }
    }
    REQUIRE(first.severity == minimum);

    REQUIRE(edge_criticality("external/e2", {first}) == first.severity);
    REQUIRE(render_repair_set_text(first).find("replace_triggering_relation") != std::string::npos);

    std::filesystem::remove_all(root);
}

TEST_CASE("breakpoint rank leaves non-repairable breakpoints unresolved instead of failing") {
    const auto root = temp_breakpoint_measure_repo();
    auto repository = Repository::init(root);
    (void)repository.create_branch("main", World{"seed"});
    REQUIRE(repository.commit("main", object_tx(repository.mutable_world("main"), "A"), Verifier{})->accepted);
    (void)repository.fork("main", "experiment");
    REQUIRE(repository.commit("experiment", object_tx(repository.mutable_world("experiment"), "B"), Verifier{})->accepted);

    ProjectionStore store{repository};
    const auto ranked = BreakpointMeasure{}.rank(repository, store, "experiment");
    const auto divergence = std::ranges::find_if(ranked, [](const BreakpointMeasurement& measurement) {
        return measurement.breakpoint.kind == BreakpointKind::BranchDivergence;
    });

    REQUIRE(divergence != ranked.end());
    REQUIRE_FALSE(divergence->has_eliminating_repair);
    REQUIRE(divergence->repairs.empty());

    std::filesystem::remove_all(root);
}

TEST_CASE("breakpoint intervention-cost rank is deterministic") {
    const auto root = temp_breakpoint_measure_repo();
    auto repository = Repository::init(root);
    (void)seed_repeated(repository);
    ProjectionStore store{repository};

    const auto first = BreakpointMeasure{}.rank(repository, store, "main");
    const auto second = BreakpointMeasure{}.rank(repository, store, "main");

    REQUIRE(first.size() == second.size());
    REQUIRE_FALSE(first.empty());
    REQUIRE(first.front().breakpoint.id == second.front().breakpoint.id);
    REQUIRE(first.front().severity == second.front().severity);
    REQUIRE(render_breakpoint_rank_text("main", first).find("intervention-cost") != std::string::npos);

    std::filesystem::remove_all(root);
}
