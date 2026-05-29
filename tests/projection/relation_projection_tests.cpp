// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

#include "pv/ingest/graph_log_importer.hpp"
#include "pv/ingest/ingestion_index.hpp"
#include "pv/projection/projection_store.hpp"
#include "pv/projection/relation_projection.hpp"
#include "pv/storage/repository.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_relation_repo() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_relation_projection_" + std::to_string(stamp));
    std::filesystem::remove_all(path);
    return path;
}

}  // namespace

TEST_CASE("relation projection is deterministic and counts repeated relations") {
    const auto root = temp_relation_repo();
    auto repository = Repository::init(root);
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

    ProjectionStore store{repository};
    const auto first_projection = RelationProjection{}.project(store, "main");
    const auto second_projection = RelationProjection{}.project(store, "main");
    REQUIRE(first_projection == second_projection);

    REQUIRE(first_projection.size() == 1);
    REQUIRE(first_projection.front().relation == "causes");
    REQUIRE(first_projection.front().occurrences == 2);
    REQUIRE(first_projection.front().evidence_event_ids.size() == 2);

    std::filesystem::remove_all(root);
}
