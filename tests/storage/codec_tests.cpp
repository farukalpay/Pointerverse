// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "pv/core/world.hpp"
#include "pv/hash/hasher.hpp"
#include "pv/runtime/world_store.hpp"
#include "pv/kernel/canonical_codec.hpp"
#include "pv/storage/object_codec.hpp"

using namespace pv;

namespace {

template <class T, class Decode>
T round_trip(const T& value, Decode decode) {
    auto bytes = canonical_encode(value);
    CanonicalReader reader{bytes};
    auto decoded = decode(reader);
    reader.expect_end();
    return decoded;
}

}  // namespace

TEST_CASE("canonical codec round trips snapshots deltas traces and laws") {
    World world{"seed"};
    Verifier verifier;
    auto delta = world.object_delta("A", "Node");
    REQUIRE(world.commit(delta, verifier).accepted);

    const auto snapshot = world.snapshot();
    const auto decoded_snapshot = round_trip(snapshot, decode_world_snapshot);
    REQUIRE(decoded_snapshot.canonical_hash() == snapshot.canonical_hash());
    REQUIRE(sha256(canonical_encode(snapshot)) == snapshot.canonical_hash());

    const auto decoded_delta = round_trip(delta, decode_delta);
    REQUIRE(canonical_hash(decoded_delta) == canonical_hash(delta));

    const auto events = world.trace().events();
    const auto decoded_events = round_trip(events, decode_trace_events);
    REQUIRE(canonical_hash(decoded_events) == canonical_hash(events));

    std::vector<LawStatus> statuses{{"law", true, Severity::Info, 0.0, "ok"}};
    const auto decoded_statuses = round_trip(statuses, decode_law_statuses);
    REQUIRE(canonical_hash(decoded_statuses) == canonical_hash(statuses));

    std::vector<LawViolation> violations{{"law", Severity::Error, 1.0, "bad"}};
    const auto decoded_violations = round_trip(violations, decode_law_violations);
    REQUIRE(canonical_hash(decoded_violations) == canonical_hash(violations));
}

TEST_CASE("stored commit canonical bytes are the commit id root") {
    WorldStore store;
    const auto main = store.create_branch("main", World{"seed"});
    auto tx = Transaction{};
    tx.label = "object A";
    tx.delta = store.mutable_world(main).object_delta("A", "Node");
    const auto record = store.commit(main, tx, Verifier{});
    REQUIRE(record.has_value());

    const auto stored = make_stored_commit(*record);
    const auto bytes = canonical_encode(stored);
    REQUIRE(sha256(bytes) == record->id.value);

    CanonicalReader reader{bytes};
    auto decoded = decode_stored_commit(reader);
    reader.expect_end();
    decoded.record.id = CommitId{sha256(bytes)};
    REQUIRE(decoded.record.id == record->id);
    REQUIRE(make_commit_id(decoded.record) == record->id);
}
