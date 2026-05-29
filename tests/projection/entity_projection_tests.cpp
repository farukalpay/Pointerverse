// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

#include "pv/ingest/graph_log_importer.hpp"
#include "pv/ingest/ingestion_index.hpp"
#include "pv/projection/entity_projection.hpp"
#include "pv/projection/projection_store.hpp"
#include "pv/storage/repository.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_entity_repo() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_entity_projection_" + std::to_string(stamp));
    std::filesystem::remove_all(path);
    return path;
}

}  // namespace

TEST_CASE("entity projection is deterministic and carries evidence ids") {
    const auto root = temp_entity_repo();
    auto repository = Repository::init(root);
    IngestionIndex index{repository.root()};
    IngestionOptions options;
    options.branch = "main";

    GraphEvent event;
    event.id = "e1";
    event.source = "external";
    event.from = "A";
    event.from_type = "Entity";
    event.to = "B";
    event.to_type = "Entity";
    event.relation = "causes";
    REQUIRE(GraphLogImporter{repository}.import({event}, index, options).accepted == 1);

    ProjectionStore store{repository};
    const auto first = EntityProjection{}.project(store, "main");
    const auto second = EntityProjection{}.project(store, "main");
    REQUIRE(first == second);

    bool found = false;
    for (const auto& entry : first) {
        if (entry.entity == "A") {
            found = true;
            REQUIRE(entry.evidence_event_ids.size() == 1);
            REQUIRE(entry.evidence_event_ids.front() == "external/e1");
        }
    }
    REQUIRE(found);

    std::filesystem::remove_all(root);
}
