// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <utility>

#include "pv/breakpoint/breakpoint_finder.hpp"
#include "pv/breakpoint/evidence_chain.hpp"
#include "pv/ingest/graph_log_importer.hpp"
#include "pv/ingest/ingestion_index.hpp"
#include "pv/projection/projection_store.hpp"
#include "pv/storage/repository.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_chain_repo() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_breakpoint_chain_" + std::to_string(stamp));
    std::filesystem::remove_all(path);
    return path;
}

GraphEvent chain_event(std::string id, std::string from, std::string to) {
    GraphEvent event;
    event.id = std::move(id);
    event.source = "external";
    event.from = std::move(from);
    event.from_type = "Entity";
    event.to = std::move(to);
    event.to_type = "Entity";
    event.relation = "causes";
    return event;
}

}  // namespace

TEST_CASE("evidence chain names trigger prior events affected graph and evidence ids") {
    const auto root = temp_chain_repo();
    auto repository = Repository::init(root);
    IngestionIndex index{repository.root()};
    IngestionOptions options;
    options.branch = "main";
    REQUIRE(GraphLogImporter{repository}.import(
        {chain_event("e1", "A", "B"), chain_event("e2", "A", "C")},
        index,
        options).accepted == 2);

    ProjectionStore store{repository};
    const auto breakpoints = BreakpointFinder{}.find(store, "main");
    const auto repeated = std::ranges::find_if(breakpoints, [](const Breakpoint& breakpoint) {
        return breakpoint.kind == BreakpointKind::RepeatedRelation;
    });
    REQUIRE(repeated != breakpoints.end());

    const auto chain = EvidenceChainBuilder{}.build(store, "main", *repeated);
    REQUIRE(chain.triggering_event.evidence_id == "external/e2");
    REQUIRE_FALSE(chain.prior_enabling_events.empty());
    REQUIRE(std::ranges::any_of(chain.prior_enabling_events, [](const EvidenceChainStep& step) {
        return step.evidence_id == "external/e1";
    }));
    REQUIRE(std::ranges::find(chain.affected_relations, "causes") != chain.affected_relations.end());
    REQUIRE(std::ranges::find(chain.evidence_ids, "external/e1") != chain.evidence_ids.end());
    REQUIRE(std::ranges::find(chain.evidence_ids, "external/e2") != chain.evidence_ids.end());

    const auto rendered = render_evidence_chain_text(chain);
    REQUIRE(rendered.find("triggering event") != std::string::npos);
    REQUIRE(rendered.find("prior enabling events") != std::string::npos);
    REQUIRE(rendered.find("affected entities") != std::string::npos);
    REQUIRE(rendered.find("evidence ids") != std::string::npos);

    std::filesystem::remove_all(root);
}
