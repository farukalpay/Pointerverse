// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

#include "pv/core/world.hpp"
#include "pv/kernel/canonical_codec.hpp"
#include "pv/storage/repository.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_repo_path(std::string_view name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_attr_" + std::string{name} + "_" + std::to_string(stamp));
    std::filesystem::remove_all(path);
    return path;
}

const Attribute* attribute_named(const std::vector<Attribute>& attributes, std::string_view key) {
    for (const auto& attribute : attributes) {
        if (attribute.key == key) {
            return &attribute;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("object and pointer attributes survive snapshot codec and repository reopen") {
    const auto root = temp_repo_path("attrs");
    auto repo = Repository::init(root);
    (void)repo.create_branch("main", World{"kernel"});

    auto& world = repo.mutable_world("main");
    Delta create;
    create.append_create(ObjectCreate{
        TempObjectId{1},
        "FileA",
        world.type_id("File"),
        ExistenceState::Alive,
        {Attribute{"path", string_value("src/main.cpp")}, Attribute{"line", uint64_value(12)}}
    });
    create.append_create(ObjectCreate{
        TempObjectId{2},
        "Agent0",
        world.type_id("Agent"),
        ExistenceState::Alive,
        {Attribute{"actor", string_value("codex")}}
    });
    create.append_link(PointerCreate{
        ObjectRef{TempObjectId{2}},
        ObjectRef{TempObjectId{1}},
        world.relation_type("modifies"),
        CausalRole::Structural,
        Weight{1.0},
        "kernel",
        {Attribute{"risk", float64_value(0.75)}}
    });

    Transaction tx;
    tx.label = "typed attributes";
    tx.delta = create;
    REQUIRE(repo.commit("main", tx, Verifier{})->accepted);

    const auto snapshot = repo.world("main").snapshot();
    REQUIRE(snapshot.facts.size() >= 4);
    const auto* file = snapshot.object(repo.world("main").object_by_name("FileA"));
    REQUIRE(file != nullptr);
    REQUIRE(attribute_named(file->attributes, "path")->value == string_value("src/main.cpp"));
    REQUIRE(attribute_named(file->attributes, "line")->value == uint64_value(12));
    REQUIRE(attribute_named(snapshot.pointers.front().attributes, "risk")->value == float64_value(0.75));

    const auto snapshot_bytes = canonical_encode(snapshot);
    CanonicalReader reader{snapshot_bytes};
    const auto decoded = decode_world_snapshot(reader);
    reader.expect_end();
    REQUIRE(decoded.canonical_hash() == snapshot.canonical_hash());
    const auto decoded_has_actor =
        attribute_named(decoded.objects.front().attributes, "actor") != nullptr
        || attribute_named(decoded.objects.back().attributes, "actor") != nullptr;
    REQUIRE(decoded_has_actor);

    const auto reopened = Repository::open(root);
    const auto reopened_snapshot = reopened.world("main").snapshot();
    REQUIRE(reopened_snapshot.canonical_hash() == snapshot.canonical_hash());
    REQUIRE(attribute_named(reopened_snapshot.pointer(PointerId{1})->attributes, "risk")->value == float64_value(0.75));
    std::filesystem::remove_all(root);
}
