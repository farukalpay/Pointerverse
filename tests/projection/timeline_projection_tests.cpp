// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

#include "pv/ingest/graph_log_importer.hpp"
#include "pv/ingest/ingestion_index.hpp"
#include "pv/projection/projection_store.hpp"
#include "pv/projection/timeline_projection.hpp"
#include "pv/storage/repository.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_projection_repo(std::string_view name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path()
        / ("pointerverse_projection_" + std::string{name} + "_" + std::to_string(stamp));
    std::filesystem::remove_all(path);
    return path;
}

void ingest_projection_events(Repository& repository) {
    IngestionIndex index{repository.root()};
    IngestionOptions options;
    options.branch = "main";
    GraphEvent first;
    first.id = "e1";
    first.source = "external";
    first.from = "A";
    first.from_type = "Entity";
    first.to = "B";
    first.to_type = "Entity";
    first.relation = "causes";
    GraphEvent second = first;
    second.id = "e2";
    second.to = "C";
    REQUIRE(GraphLogImporter{repository}.import({first, second}, index, options).accepted == 2);
}

}  // namespace

TEST_CASE("timeline projection is stable for the same branch state") {
    const auto root = temp_projection_repo("timeline");
    auto repository = Repository::init(root);
    ingest_projection_events(repository);

    ProjectionStore store{repository};
    const auto first = TimelineProjection{}.project(store, "main");
    const auto second = TimelineProjection{}.project(store, "main");

    REQUIRE(first == second);
    REQUIRE(render_timeline_projection_text("main", first).find("Timeline projection: main") != std::string::npos);

    std::filesystem::remove_all(root);
}
