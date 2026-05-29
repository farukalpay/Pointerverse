// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

#include "pv/core/world.hpp"
#include "pv/storage/chunked_snapshot_store.hpp"
#include "pv/storage/content_store.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_repo_path(std::string_view name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_chunked_" + std::string{name} + "_" + std::to_string(stamp));
    std::filesystem::remove_all(path);
    return path;
}

}  // namespace

TEST_CASE("chunked snapshot store round trips deterministic roots") {
    const auto root = temp_repo_path("roundtrip");
    ContentStore objects{root};
    ChunkedSnapshotStore chunks{objects, 1};

    World world{"seed"};
    REQUIRE(world.commit(world.object_delta("A", "Node"), Verifier{}).accepted);
    REQUIRE(world.commit(world.object_delta("B", "Node"), Verifier{}).accepted);
    REQUIRE(world.commit(
        world.link_delta(world.object_by_name("A"), world.object_by_name("B"), "causes", 1.0, CausalRole::Structural),
        Verifier{}).accepted);

    const auto snapshot = world.snapshot();
    const auto first = chunks.put_snapshot(snapshot);
    const auto second = chunks.put_snapshot(snapshot);
    REQUIRE(first == second);
    REQUIRE(chunks.get_snapshot(first).canonical_hash() == snapshot.canonical_hash());
    std::filesystem::remove_all(root);
}

TEST_CASE("chunked snapshot pages reuse unaffected object pages") {
    World left{"seed"};
    REQUIRE(left.commit(left.object_delta("A", "Node"), Verifier{}).accepted);
    REQUIRE(left.commit(left.object_delta("B", "Node"), Verifier{}).accepted);

    World right = left;
    Delta update;
    update.append_update(ObjectUpdate{ObjectRef{right.object_by_name("A")}, right.type_id("Region"), std::nullopt});
    REQUIRE(right.commit(update, Verifier{}).accepted);

    const auto left_plan = build_chunked_snapshot_plan(left.snapshot(), 1);
    const auto right_plan = build_chunked_snapshot_plan(right.snapshot(), 1);
    REQUIRE(left_plan.root_object != right_plan.root_object);
    REQUIRE(left_plan.object_page_hashes.size() == right_plan.object_page_hashes.size());
    REQUIRE(left_plan.object_page_hashes.at(1) == right_plan.object_page_hashes.at(1));
    REQUIRE(left_plan.object_page_hashes.at(0) != right_plan.object_page_hashes.at(0));
}
