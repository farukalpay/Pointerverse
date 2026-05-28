cmake_minimum_required(VERSION 3.24)

project(Pointerverse VERSION 0.1.0 LANGUAGES CXX)

option(POINTERVERSE_BUILD_TESTS "Build Pointerverse tests" ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(fmt CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

add_library(pointerverse_core
    src/audit/report.cpp
    src/audit/risk_score.cpp
    src/audit/timeline.cpp
    src/audit/violation_report.cpp
    src/category/composition.cpp
    src/cli/script.cpp
    src/core/id.cpp
    src/core/object.cpp
    src/core/relation.cpp
    src/core/snapshot.cpp
    src/core/type.cpp
    src/core/world.cpp
    src/domain/agent_audit.cpp
    src/domain/domain.cpp
    src/domain/package.cpp
    src/domain/schema.cpp
    src/guard/finding.cpp
    src/guard/git_diff_adapter.cpp
    src/guard/guard_pipeline.cpp
    src/guard/policy_pack.cpp
    src/guard/report.cpp
    src/hash/canonical.cpp
    src/hash/hasher.cpp
    src/ingest/agent_audit_adapter.cpp
    src/ingest/evidence.cpp
    src/ingest/event_schema.cpp
    src/ingest/ingestion_index.cpp
    src/ingest/ingestion_pipeline.cpp
    src/ingest/jsonl_adapter.cpp
    src/ingest/normalizer.cpp
    src/law/invariant.cpp
    src/law/verifier.cpp
    src/observer/observer.cpp
    src/query/explanation.cpp
    src/query/graph_query.cpp
    src/query/history_query.cpp
    src/query/query.cpp
    src/rule/pattern.cpp
    src/rule/rule.cpp
    src/rule/rule_engine.cpp
    src/runtime/commit_graph.cpp
    src/runtime/commit_record.cpp
    src/runtime/ids.cpp
    src/runtime/merge.cpp
    src/runtime/replayer.cpp
    src/runtime/snapshot_store.cpp
    src/runtime/transaction.cpp
    src/runtime/world_store.cpp
    src/storage/canonical_codec.cpp
    src/storage/content_store.cpp
    src/storage/integrity.cpp
    src/storage/manifest.cpp
    src/storage/object_codec.cpp
    src/storage/ref_store.cpp
    src/storage/repository.cpp
    src/storage/wal.cpp
    src/trace/recorder.cpp
    src/trace/replayer.cpp
)

target_include_directories(pointerverse_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(pointerverse_core
    PUBLIC
        fmt::fmt
        nlohmann_json::nlohmann_json
        OpenSSL::Crypto
)

target_compile_features(pointerverse_core PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(pointerverse_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(pointerverse_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(pointerverse apps/pointerverse/main.cpp)
target_link_libraries(pointerverse
    PRIVATE
        pointerverse_core
        CLI11::CLI11
        fmt::fmt
)

if(POINTERVERSE_BUILD_TESTS)
    enable_testing()
    find_package(Catch2 3 CONFIG REQUIRED)

    add_executable(pointerverse_tests
        tests/category/category_tests.cpp
        tests/cli/cli_tests.cpp
        tests/core/core_tests.cpp
        tests/core/delta_tests.cpp
        tests/domain/agent_audit_tests.cpp
        tests/guard/git_diff_adapter_tests.cpp
        tests/guard/guard_pipeline_tests.cpp
        tests/guard/policy_pack_tests.cpp
        tests/hash/canonical_hash_tests.cpp
        tests/ingest/agent_audit_ingestion_tests.cpp
        tests/ingest/evidence_tests.cpp
        tests/ingest/idempotency_tests.cpp
        tests/ingest/jsonl_adapter_tests.cpp
        tests/audit/report_tests.cpp
        tests/law/law_tests.cpp
        tests/query/query_tests.cpp
        tests/rule/rule_engine_tests.cpp
        tests/runtime/branch_tests.cpp
        tests/runtime/transaction_tests.cpp
        tests/storage/codec_tests.cpp
        tests/storage/content_store_tests.cpp
        tests/storage/integrity_tests.cpp
        tests/storage/repository_tests.cpp
        tests/storage/wal_tests.cpp
        tests/trace/replay_tests.cpp
    )
    target_link_libraries(pointerverse_tests
        PRIVATE
            pointerverse_core
            Catch2::Catch2WithMain
    )
    target_compile_definitions(pointerverse_tests
        PRIVATE
            POINTERVERSE_CLI_PATH="$<TARGET_FILE:pointerverse>"
    )

    include(Catch)
    catch_discover_tests(pointerverse_tests)
endif()
