// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "pv/core/value.hpp"
#include "pv/storage/canonical_codec.hpp"

using namespace pv;

namespace {

Value round_trip(const Value& value) {
    auto bytes = canonical_encode(value);
    CanonicalReader reader{bytes};
    auto decoded = decode_value(reader);
    reader.expect_end();
    return decoded;
}

}  // namespace

TEST_CASE("typed values round trip through canonical codec") {
    REQUIRE(round_trip(null_value()) == null_value());
    REQUIRE(round_trip(bool_value(true)) == bool_value(true));
    REQUIRE(round_trip(int64_value(-42)) == int64_value(-42));
    REQUIRE(round_trip(uint64_value(42)) == uint64_value(42));
    REQUIRE(round_trip(float64_value(-0.0)) == float64_value(0.0));
    REQUIRE(round_trip(string_value("src/main.cpp")) == string_value("src/main.cpp"));

    Hash256 hash;
    hash.value[31] = std::byte{0x7b};
    REQUIRE(round_trip(hash_value(hash)) == hash_value(hash));
    REQUIRE(round_trip(object_ref_value(ObjectId{7, 2})) == object_ref_value(ObjectId{7, 2}));
}

TEST_CASE("typed values have deterministic kind-aware ordering") {
    REQUIRE(null_value() < bool_value(false));
    REQUIRE(int64_value(-1) < int64_value(0));
    REQUIRE(string_value("a") < string_value("b"));
    REQUIRE(object_ref_value(ObjectId{1, 1}) < object_ref_value(ObjectId{2, 1}));
}
