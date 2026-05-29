// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <string>
#include <utility>

#include "pv/breakpoint/breakpoint_finder.hpp"
#include "pv/breakpoint/repair_candidate.hpp"
#include "pv/cli/script.hpp"
#include "pv/ingest/graph_log_importer.hpp"
#include "pv/ingest/ingestion_index.hpp"
#include "pv/projection/projection_store.hpp"
#include "pv/storage/repository.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_repair_repo() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_breakpoint_repair_" + std::to_string(stamp));
    std::filesystem::remove_all(path);
    return path;
}

GraphEvent repair_event(std::string id) {
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

bool active_at(const PointerSnapshot& pointer, Epoch epoch) noexcept {
    return pointer.born_at <= epoch && (!pointer.expires_at.has_value() || epoch < *pointer.expires_at);
}

std::size_t active_relation_count(const WorldSnapshot& snapshot, std::string_view relation) {
    return static_cast<std::size_t>(std::ranges::count_if(snapshot.pointers, [&](const PointerSnapshot& pointer) {
        return active_at(pointer, snapshot.epoch) && snapshot.relation_name(pointer.relation) == relation;
    }));
}

}  // namespace

TEST_CASE("repair candidate emits an executable minimal graph repair script") {
    const auto root = temp_repair_repo();
    auto repository = Repository::init(root);
    IngestionIndex index{repository.root()};
    IngestionOptions options;
    options.branch = "main";
    REQUIRE(GraphLogImporter{repository}.import({repair_event("e1"), repair_event("e2")}, index, options).accepted == 2);

    ProjectionStore store{repository};
    const auto breakpoints = BreakpointFinder{}.find(store, "main");
    const auto repeated = std::ranges::find_if(breakpoints, [](const Breakpoint& breakpoint) {
        return breakpoint.kind == BreakpointKind::RepeatedRelation;
    });
    REQUIRE(repeated != breakpoints.end());

    const auto candidate = RepairCandidateBuilder{}.build(store, "main", *repeated);
    REQUIRE(candidate.action == RepairAction::RemoveTriggeringRelation);
    REQUIRE_FALSE(candidate.evidence_ids.empty());
    REQUIRE(candidate.script.find("unlink A -> B : causes") != std::string::npos);

    (void)repository.fork("main", "repaired");
    std::istringstream input{candidate.script};
    std::ostringstream output;
    cli::ScriptEngine engine{repository, "repaired"};
    REQUIRE(engine.run_stream(input, output, false));
    REQUIRE(active_relation_count(repository.world("repaired").snapshot(), "causes") == 1);

    auto without_evidence = *repeated;
    without_evidence.evidence_ids.clear();
    REQUIRE_THROWS_AS(RepairCandidateBuilder{}.build(store, "main", without_evidence), std::invalid_argument);

    std::filesystem::remove_all(root);
}
