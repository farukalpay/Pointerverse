// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <sstream>

#include "pv/core/value.hpp"
#include "pv/ingest/agent_audit_adapter.hpp"
#include "pv/ingest/graph_log_importer.hpp"
#include "pv/ingest/ingestion_index.hpp"
#include "pv/ingest/ingestion_pipeline.hpp"
#include "pv/ingest/jsonl_adapter.hpp"
#include "pv/query/query.hpp"
#include "pv/storage/repository.hpp"

using namespace pv;

namespace {

std::filesystem::path temp_repo_path(std::string_view name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("pointerverse_ingest_" + std::string{name} + "_" + std::to_string(stamp));
    std::filesystem::remove_all(path);
    return path;
}

EvidenceEvent event(std::string id, std::string actor, std::string action, std::string target) {
    EvidenceEvent out;
    out.source = "agent-log";
    out.event_id = std::move(id);
    out.actor = std::move(actor);
    out.action = std::move(action);
    out.target = std::move(target);
    return out;
}

}  // namespace

TEST_CASE("agent audit adapter maps supported events to normalized relations") {
    const AgentAuditAdapter adapter;

    auto read = adapter.normalize(event("1", "Agent0", "read_file", "src/main.cpp"));
    REQUIRE(read.from == "Agent0");
    REQUIRE(read.from_type == "Agent");
    REQUIRE(read.relation == "reads");
    REQUIRE(read.to == "src/main.cpp");
    REQUIRE(read.to_type == "File");

    EvidenceEvent test;
    test.source = "agent-log";
    test.event_id = "2";
    test.action = "ci.test_passed";
    test.attributes.push_back({"pr", "PR42"});
    test.attributes.push_back({"test", "Tests"});
    auto normalized_test = adapter.normalize(test);
    REQUIRE(normalized_test.from == "PR42");
    REQUIRE(normalized_test.from_type == "PullRequest");
    REQUIRE(normalized_test.relation == "tests");
    REQUIRE(normalized_test.to == "Tests");

    auto created = adapter.normalize(event("3", "Agent0", "create_file", "src/new.cpp"));
    REQUIRE(created.relation == "creates");
    REQUIRE(created.to_type == "File");

    auto deleted = adapter.normalize(event("4", "Agent0", "delete_file", "src/old.cpp"));
    REQUIRE(deleted.relation == "deletes");
    REQUIRE(deleted.to == "src/old.cpp");

    auto renamed = adapter.normalize(event("5", "Agent0", "rename_file", "src/newer.cpp"));
    REQUIRE(renamed.relation == "renames");
    REQUIRE(renamed.to == "src/newer.cpp");
}

TEST_CASE("agent audit ingestion accepts observe violations and skips duplicate events") {
    const auto root = temp_repo_path("pipeline");
    auto repository = Repository::init(root);

    std::istringstream input{
        "{\"id\":\"1\",\"agent\":\"Agent0\",\"event\":\"read_file\",\"path\":\"src/main.cpp\",\"ts\":1710000000}\n"
        "{\"id\":\"1\",\"agent\":\"Agent0\",\"event\":\"read_file\",\"path\":\"src/main.cpp\",\"ts\":1710000000}\n"
        "{\"id\":\"2\",\"agent\":\"Agent0\",\"event\":\"write_file\",\"path\":\"src/main.cpp\",\"ts\":1710000001}\n"
        "{\"id\":\"3\",\"agent\":\"Agent0\",\"event\":\"create_pr\",\"pr\":\"PR42\",\"ts\":1710000002}\n"
    };
    const auto batch = JsonlEvidenceAdapter{"agent-log"}.read(input);
    REQUIRE(batch.errors.empty());

    IngestionIndex index{repository.root()};
    IngestionOptions options;
    options.branch = "main";
    options.mode = VerificationMode::Observe;
    const auto result = IngestionPipeline{repository}.ingest(batch.events, AgentAuditAdapter{}, index, options);

    REQUIRE(result.events_read == 4);
    REQUIRE(result.accepted == 3);
    REQUIRE(result.skipped_duplicates == 1);
    REQUIRE(result.rejected == 0);
    REQUIRE(result.errors == 0);
    REQUIRE(result.violations == 1);

    const auto snapshot = repository.world("main").snapshot();
    const QueryEngine query;
    REQUIRE(query.objects_by_type(snapshot, "Agent").objects.size() == 1);
    REQUIRE(query.objects_by_type(snapshot, "Evidence").objects.size() == 3);
    REQUIRE(query.links_by_relation(snapshot, "reads").pointers.size() == 1);
    REQUIRE(query.links_by_relation(snapshot, "modifies").pointers.size() == 1);
    REQUIRE(query.links_by_relation(snapshot, "backs").pointers.size() == 3);

    std::filesystem::remove_all(root);
}

TEST_CASE("graph-log importer builds a typed world from a generic event stream") {
    const auto root = temp_repo_path("graphlog");
    auto repository = Repository::init(root);

    std::istringstream input{
        "{\"id\":\"e1\",\"from\":\"Suleiman_I\",\"from_type\":\"Sultan\",\"to\":\"OttomanArmy\",\"to_type\":\"Army\",\"relation\":\"commands\",\"troops\":50000}\n"
        "{\"id\":\"e2\",\"from\":\"OttomanArmy\",\"from_type\":\"Army\",\"to\":\"Louis_II\",\"to_type\":\"King\",\"relation\":\"defeats\",\"role\":\"Generative\"}\n"
        "{\"id\":\"e1\",\"from\":\"dup\",\"to\":\"dup\",\"relation\":\"dup\"}\n"
    };

    IngestionIndex index{repository.root()};
    IngestionOptions options;
    options.branch = "main";
    options.mode = VerificationMode::Observe;
    const auto result = GraphLogImporter{repository}.import(input, index, options);

    REQUIRE(result.events_read == 3);
    REQUIRE(result.accepted == 2);
    REQUIRE(result.skipped_duplicates == 1);
    REQUIRE(result.errors == 0);

    const auto snapshot = repository.world("main").snapshot();
    const QueryEngine query;
    REQUIRE(query.objects_by_type(snapshot, "Sultan").objects.size() == 1);
    REQUIRE(query.objects_by_type(snapshot, "Army").objects.size() == 1);
    REQUIRE(query.links_by_relation(snapshot, "defeats").pointers.size() == 1);

    // An extra scalar field on the event rides along as a typed pointer attribute.
    const auto commands = query.links_by_relation(snapshot, "commands");
    REQUIRE(commands.pointers.size() == 1);
    const auto* pointer = snapshot.pointer(commands.pointers.front());
    REQUIRE(pointer != nullptr);
    bool troops_present = false;
    for (const auto& attribute : pointer->attributes) {
        if (attribute.key == "troops") {
            troops_present = attribute.value.kind == ValueKind::UInt64;
        }
    }
    REQUIRE(troops_present);

    std::filesystem::remove_all(root);
}

TEST_CASE("strict ingestion rejects policy violations without marking idempotency") {
    const auto root = temp_repo_path("strict");
    auto repository = Repository::init(root);

    EvidenceEvent create_pr;
    create_pr.source = "agent-log";
    create_pr.event_id = "pr-1";
    create_pr.actor = "Agent0";
    create_pr.action = "create_pr";
    create_pr.target = "PR42";

    IngestionIndex index{repository.root()};
    IngestionOptions options;
    options.branch = "main";
    options.mode = VerificationMode::Strict;
    const auto result = IngestionPipeline{repository}.ingest({create_pr}, AgentAuditAdapter{}, index, options);

    REQUIRE(result.accepted == 0);
    REQUIRE(result.rejected == 1);
    REQUIRE(result.violations == 1);
    REQUIRE_FALSE(index.seen("agent-log", "pr-1"));

    std::filesystem::remove_all(root);
}
