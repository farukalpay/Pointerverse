// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

#include "pv/hash/canonical.hpp"
#include "pv/ingest/ingestion_index.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_index_path() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_ingest_index_" + std::to_string(stamp));
    std::filesystem::remove_all(path);
    return path;
}

CommitId commit_id_for_test() {
    auto hash = parse_hash256("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    REQUIRE(hash.has_value());
    return CommitId{*hash};
}

}  // namespace

TEST_CASE("ingestion index persists source event idempotency keys") {
    const auto root = temp_index_path();

    {
        IngestionIndex index{root};
        REQUIRE_FALSE(index.seen("agent-log", "1"));
        index.mark_seen("agent-log", "1", commit_id_for_test());
        REQUIRE(index.seen("agent-log", "1"));
    }

    {
        IngestionIndex index{root};
        REQUIRE(index.seen("agent-log", "1"));
        REQUIRE_FALSE(index.seen("agent-log", "2"));
    }

    std::filesystem::remove_all(root);
}
