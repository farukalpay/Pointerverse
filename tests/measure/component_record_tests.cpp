// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "pv/kernel/canonical_codec.hpp"
#include "pv/measure/component_record.hpp"
#include "pv/measure/measurement_record.hpp"

using namespace pv;

namespace {

Hash256 hash_with_marker(std::byte marker) {
    Hash256 hash;
    hash.value.back() = marker;
    return hash;
}

}  // namespace

TEST_CASE("measurement component records are content addressed") {
    const auto component = make_measurement_component_record(
        "structural",
        "forward_cone_mass",
        hash_with_marker(std::byte{0x01}),
        hash_with_marker(std::byte{0x02}),
        93,
        hash_with_marker(std::byte{0x03}));

    REQUIRE(component.component_hash == measurement_component_hash(component));

    const auto decoded = decode_measurement_component_record_bytes(canonical_encode(component));
    REQUIRE(decoded == component);

    auto changed = component;
    changed.evidence_root = hash_with_marker(std::byte{0x04});
    REQUIRE(measurement_component_hash(changed) != component.component_hash);
}

TEST_CASE("measurement component root is order independent") {
    const auto left = hash_with_marker(std::byte{0x11});
    const auto right = hash_with_marker(std::byte{0x22});

    REQUIRE(measurement_component_root({left, right}) == measurement_component_root({right, left}));
    REQUIRE(measurement_component_root({left, right}) == measurement_component_root({left, right, left}));
}

TEST_CASE("measurement identity is distinct from stored record object hash") {
    const auto component = make_measurement_component_record(
        "law",
        "total_magnitude",
        hash_with_marker(std::byte{0x01}),
        hash_with_marker(std::byte{0x02}),
        2,
        hash_with_marker(std::byte{0x03}));
    const auto record = make_measurement_record(
        CommitId{hash_with_marker(std::byte{0x41})},
        hash_with_marker(std::byte{0x42}),
        hash_with_marker(std::byte{0x43}),
        {component.component_hash},
        {component.evidence_root});

    REQUIRE(record.measurement_identity_hash == measurement_identity_hash(record));
    REQUIRE(record.measurement_object_hash == measurement_record_hash(record));
    REQUIRE(record.measurement_identity_hash != record.measurement_object_hash);

    auto changed_component_root = record;
    changed_component_root.component_root = hash_with_marker(std::byte{0x50});
    REQUIRE(measurement_identity_hash(changed_component_root) != record.measurement_identity_hash);

    auto changed_evidence_root = record;
    changed_evidence_root.evidence_root = hash_with_marker(std::byte{0x51});
    REQUIRE(measurement_identity_hash(changed_evidence_root) != record.measurement_identity_hash);
}
