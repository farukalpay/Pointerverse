// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

#include "pv/ingest/graph_log_importer.hpp"
#include "pv/ingest/ingestion_index.hpp"
#include "pv/normalize/graph_event_encoder.hpp"
#include "pv/storage/repository.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_repo_path() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_canonical_ingest_" + std::to_string(stamp));
    std::filesystem::remove_all(path);
    return path;
}

GraphEvent graph_event() {
    CanonicalEvent event;
    event.id = "c1";
    event.source = "external";
    event.kind = "observation";
    event.actor = "A";
    event.subject = "B";
    event.relation = "causes";
    return GraphEventEncoder{}.encode(event);
}

}  // namespace

TEST_CASE("canonical graph ingest skips duplicate source event ids") {
    const auto root = temp_repo_path();
    auto repository = Repository::init(root);
    IngestionIndex index{repository.root()};
    IngestionOptions options;
    options.branch = "main";
    options.mode = VerificationMode::Observe;

    const auto event = graph_event();
    const auto result = GraphLogImporter{repository}.import({event, event}, index, options);

    REQUIRE(result.events_read == 2);
    REQUIRE(result.accepted == 1);
    REQUIRE(result.skipped_duplicates == 1);
    REQUIRE(result.errors == 0);
    REQUIRE(index.seen("external", "c1"));
    REQUIRE(repository.history("main").size() == 2);

    std::filesystem::remove_all(root);
}
