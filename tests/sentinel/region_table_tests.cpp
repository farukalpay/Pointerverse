// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "pv/hash/hasher.hpp"
#include "pv/sentinel/region_table.hpp"
#include "pv/storage/canonical_codec.hpp"

using namespace pv;

namespace {

Hash256 named_hash(std::string_view value) {
    CanonicalWriter writer;
    writer.string(value);
    return sha256(writer.bytes());
}

}  // namespace

TEST_CASE("region table reports required and optional mismatches") {
    RegionTable table;
    table.add(IntegrityRegion{RegionKind::Manifest, "manifest.json", named_hash("a"), named_hash("a"), true});
    table.add(IntegrityRegion{RegionKind::BranchRef, "main", named_hash("a"), named_hash("b"), true});
    table.add(IntegrityRegion{RegionKind::TraceObject, "trace", named_hash("c"), named_hash("d"), false});

    const auto report = table.verify();

    REQUIRE_FALSE(report.clean());
    REQUIRE(report.regions_checked == 3);
    REQUIRE(report.mismatches == 2);
    REQUIRE(report.warnings == 1);
    REQUIRE(report.errors.size() == 1);
    REQUIRE(report.warning_messages.size() == 1);
}

TEST_CASE("region table root is deterministic across insertion order") {
    IntegrityRegion first{RegionKind::ProgramObject, "program", named_hash("p"), named_hash("p"), true};
    IntegrityRegion second{RegionKind::CommitObject, "commit", named_hash("c"), named_hash("c"), true};

    RegionTable left;
    left.add(first);
    left.add(second);

    RegionTable right;
    right.add(second);
    right.add(first);

    REQUIRE(left.verify().root == right.verify().root);
}
