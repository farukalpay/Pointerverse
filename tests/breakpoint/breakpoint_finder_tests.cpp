// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

#include "pv/breakpoint/breakpoint_finder.hpp"
#include "pv/ingest/graph_log_importer.hpp"
#include "pv/ingest/ingestion_index.hpp"
#include "pv/law/verifier.hpp"
#include "pv/projection/projection_store.hpp"
#include "pv/runtime/transaction_types.hpp"
#include "pv/storage/repository.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_breakpoint_repo(std::string_view name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path()
        / ("pointerverse_breakpoint_" + std::string{name} + "_" + std::to_string(stamp));
    std::filesystem::remove_all(path);
    return path;
}

GraphEvent graph_event(std::string id, std::string from, std::string to, std::string relation) {
    GraphEvent event;
    event.id = std::move(id);
    event.source = "external";
    event.from = std::move(from);
    event.from_type = "Entity";
    event.to = std::move(to);
    event.to_type = "Entity";
    event.relation = std::move(relation);
    return event;
}

void ingest_repeated_relation(Repository& repository) {
    IngestionIndex index{repository.root()};
    IngestionOptions options;
    options.branch = "main";
    REQUIRE(GraphLogImporter{repository}.import(
        {
            graph_event("e1", "A", "B", "causes"),
            graph_event("e2", "A", "B", "causes")
        },
        index,
        options).accepted == 2);
}

Transaction object_tx(World& world, std::string name) {
    Transaction tx;
    tx.label = "object " + name;
    tx.delta = world.object_delta(std::move(name), "Node");
    return tx;
}

Transaction link_tx(World& world, std::string from, std::string to, std::string relation, double weight = 1.0) {
    Transaction tx;
    tx.label = "link " + from + " " + to;
    tx.delta = world.link_delta(
        world.object_by_name(from),
        world.object_by_name(to),
        relation,
        weight,
        CausalRole::Structural);
    return tx;
}

const Breakpoint* first_kind(const std::vector<Breakpoint>& breakpoints, BreakpointKind kind) {
    const auto iter = std::ranges::find_if(breakpoints, [&](const Breakpoint& breakpoint) {
        return breakpoint.kind == kind;
    });
    return iter == breakpoints.end() ? nullptr : &*iter;
}

}  // namespace

TEST_CASE("breakpoint finder reports declared invariant violations") {
    const auto root = temp_breakpoint_repo("invariant");
    auto repository = Repository::init(root);
    (void)repository.create_branch("main", World{"seed"});

    Verifier observe{VerificationMode::Observe};
    observe.add_builtin("bounded_weight");
    REQUIRE(repository.commit("main", object_tx(repository.mutable_world("main"), "A"), observe)->accepted);
    REQUIRE(repository.commit("main", object_tx(repository.mutable_world("main"), "B"), observe)->accepted);
    const auto invalid = repository.commit(
        "main",
        link_tx(repository.mutable_world("main"), "A", "B", "causes", 1.5),
        observe);
    REQUIRE(invalid.has_value());
    REQUIRE_FALSE(invalid->violations.empty());

    ProjectionStore store{repository};
    const auto breakpoints = BreakpointFinder{}.find(store, "main");

    const auto* invariant = first_kind(breakpoints, BreakpointKind::InvariantViolation);
    REQUIRE(invariant != nullptr);
    REQUIRE_FALSE(invariant->evidence_ids.empty());
    REQUIRE(invariant->trigger.relation == "causes");
    REQUIRE(invariant->summary.find("declared invariant") != std::string::npos);

    std::filesystem::remove_all(root);
}

TEST_CASE("breakpoint finder reports stable repeated relation breakpoints with evidence ids") {
    const auto root = temp_breakpoint_repo("repeated");
    auto repository = Repository::init(root);
    ingest_repeated_relation(repository);

    ProjectionStore store{repository};
    const auto first = BreakpointFinder{}.find(store, "main");
    const auto second = BreakpointFinder{}.find(store, "main");

    REQUIRE(first == second);
    const auto* repeated = first_kind(first, BreakpointKind::RepeatedRelation);
    REQUIRE(repeated != nullptr);
    REQUIRE_FALSE(repeated->id.empty());
    REQUIRE(repeated->trigger.relation == "causes");
    REQUIRE(repeated->trigger.evidence_event_id == "external/e2");
    REQUIRE_FALSE(repeated->evidence_ids.empty());
    REQUIRE(std::ranges::find(repeated->evidence_ids, "external/e1") != repeated->evidence_ids.end());
    REQUIRE(std::ranges::find(repeated->evidence_ids, "external/e2") != repeated->evidence_ids.end());
    REQUIRE(render_breakpoints_text("main", first).find(repeated->id) != std::string::npos);

    std::filesystem::remove_all(root);
}

TEST_CASE("breakpoint finder reports abnormal concentration when one entity dominates relation endpoints") {
    const auto root = temp_breakpoint_repo("concentration");
    auto repository = Repository::init(root);
    IngestionIndex index{repository.root()};
    IngestionOptions options;
    options.branch = "main";
    REQUIRE(GraphLogImporter{repository}.import(
        {
            graph_event("e1", "Hub", "A", "touches"),
            graph_event("e2", "Hub", "B", "touches"),
            graph_event("e3", "Hub", "C", "touches"),
            graph_event("e4", "Hub", "D", "touches")
        },
        index,
        options).accepted == 4);

    ProjectionStore store{repository};
    BreakpointFindOptions options_for_find;
    options_for_find.concentration_min_events = 4;
    options_for_find.concentration_share = 0.5;
    const auto breakpoints = BreakpointFinder{}.find(store, "main", options_for_find);

    const auto* concentration = first_kind(breakpoints, BreakpointKind::AbnormalConcentration);
    REQUIRE(concentration != nullptr);
    REQUIRE(std::ranges::find(concentration->affected_entities, "Hub") != concentration->affected_entities.end());
    REQUIRE_FALSE(concentration->evidence_ids.empty());

    std::filesystem::remove_all(root);
}

TEST_CASE("breakpoint finder reports branch divergence from repository history") {
    const auto root = temp_breakpoint_repo("divergence");
    auto repository = Repository::init(root);
    (void)repository.create_branch("main", World{"seed"});
    REQUIRE(repository.commit("main", object_tx(repository.mutable_world("main"), "A"), Verifier{})->accepted);
    (void)repository.fork("main", "experiment");
    REQUIRE(repository.commit("experiment", object_tx(repository.mutable_world("experiment"), "B"), Verifier{})->accepted);

    ProjectionStore store{repository};
    const auto breakpoints = BreakpointFinder{}.find(store, "experiment");

    const auto* divergence = first_kind(breakpoints, BreakpointKind::BranchDivergence);
    REQUIRE(divergence != nullptr);
    REQUIRE_FALSE(divergence->evidence_ids.empty());
    REQUIRE(divergence->trigger.event == "branch.divergence");

    std::filesystem::remove_all(root);
}
