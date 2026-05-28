// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "pv/core/world.hpp"
#include "pv/kernel/canonical_codec.hpp"

using namespace pv;

TEST_CASE("legacy helpers produce operation batches and read-only views") {
    World world{"seed"};
    auto create = world.object_delta("A", "Node");
    REQUIRE(create.ops.size() == 1);
    REQUIRE(create.ops.front().kind == OperationKind::CreateObject);
    REQUIRE(create.creates_view().size() == 1);

    REQUIRE(world.commit(create, Verifier{}).accepted);
    auto link = world.link_delta(world.object_by_name("A"), world.object_by_name("A"), "self", 1.0, CausalRole::Structural);
    REQUIRE(link.ops.size() == 1);
    REQUIRE(link.links_view().size() == 1);
}

TEST_CASE("Delta v1 decode normalizes into Delta v2 operations") {
    CanonicalWriter writer;
    writer.string("Delta:v1");
    writer.u64(1);
    writer.u32(1);
    writer.string("A");
    writer.u32(7);
    writer.u8(static_cast<std::uint8_t>(ExistenceState::Alive));
    writer.u64(0);
    writer.u64(0);
    writer.u64(0);
    writer.u64(0);

    CanonicalReader reader{writer.bytes()};
    auto delta = decode_delta(reader);
    reader.expect_end();

    REQUIRE(delta.ops.size() == 1);
    REQUIRE(delta.ops.front().kind == OperationKind::CreateObject);
    REQUIRE(delta.creates_view().front().name == "A");
    REQUIRE(canonical_encode(delta).size() > 0);
}
