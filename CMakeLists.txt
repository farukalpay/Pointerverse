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
    src/compiler/program_builder.cpp
    src/compiler/script_compiler.cpp
    src/core/id.cpp
    src/core/object.cpp
    src/core/attribute.cpp
    src/core/fact.cpp
    src/core/operation.cpp
    src/core/relation.cpp
    src/core/snapshot.cpp
    src/core/type.cpp
    src/core/value.cpp
    src/core/world.cpp
    src/core/world_index.cpp
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
    src/kernel/executor.cpp
    src/kernel/execution_plan.cpp
    src/kernel/fact_store.cpp
    src/kernel/instruction.cpp
    src/kernel/merkle.cpp
    src/kernel/program.cpp
    src/kernel/proof.cpp
    src/kernel/vm.cpp
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
    src/sentinel/boot_gate.cpp
    src/sentinel/fault_injection.cpp
    src/sentinel/heartbeat.cpp
    src/sentinel/patrol.cpp
    src/sentinel/region_table.cpp
    src/sentinel/report.cpp
    src/sentinel/sentinel.cpp
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

add_executable(pointerverse_kernel_stress examples/kernel_stress/kernel_stress.cpp)
target_link_libraries(pointerverse_kernel_stress
    PRIVATE
        pointerverse_core
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
        tests/core/value_tests.cpp
        tests/domain/agent_audit_tests.cpp
        tests/guard/git_diff_adapter_tests.cpp
        tests/guard/guard_pipeline_tests.cpp
        tests/guard/policy_pack_tests.cpp
        tests/hash/canonical_hash_tests.cpp
        tests/ingest/agent_audit_ingestion_tests.cpp
        tests/ingest/evidence_tests.cpp
        tests/ingest/idempotency_tests.cpp
        tests/ingest/jsonl_adapter_tests.cpp
        tests/kernel/attribute_snapshot_tests.cpp
        tests/kernel/execution_plan_tests.cpp
        tests/kernel/merkle_root_tests.cpp
        tests/kernel/operation_tests.cpp
        tests/kernel/program_codec_tests.cpp
        tests/kernel/proof_tests.cpp
        tests/kernel/vm_tests.cpp
        tests/audit/report_tests.cpp
        tests/law/law_tests.cpp
        tests/query/query_tests.cpp
        tests/rule/rule_engine_tests.cpp
        tests/runtime/branch_tests.cpp
        tests/runtime/transaction_tests.cpp
        tests/sentinel/boot_gate_tests.cpp
        tests/sentinel/fault_injection_tests.cpp
        tests/sentinel/heartbeat_tests.cpp
        tests/sentinel/patrol_tests.cpp
        tests/sentinel/region_table_tests.cpp
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

    if(UNIX)
        add_test(
            NAME pointerverse_guard_action_script_syntax
            COMMAND bash -n ${CMAKE_CURRENT_SOURCE_DIR}/scripts/pointerverse-guard-action.sh
        )
        add_test(
            NAME pointerverse_guard_action_script_smoke
            COMMAND bash -c
                "rm -rf '${CMAKE_CURRENT_BINARY_DIR}/guard-action-smoke' && mkdir -p '${CMAKE_CURRENT_BINARY_DIR}/guard-action-smoke' && POINTERVERSE_BIN='$<TARGET_FILE:pointerverse>' GITHUB_WORKSPACE='${CMAKE_CURRENT_BINARY_DIR}/guard-action-smoke' POINTERVERSE_GUARD_REPO='${CMAKE_CURRENT_SOURCE_DIR}/examples/pr_guard/after' POINTERVERSE_GUARD_BASE='../before' POINTERVERSE_GUARD_COMMENT='false' '${CMAKE_CURRENT_SOURCE_DIR}/scripts/pointerverse-guard-action.sh' > '${CMAKE_CURRENT_BINARY_DIR}/guard-action-smoke/out.txt' && grep -q '::error file=config/dev.env' '${CMAKE_CURRENT_BINARY_DIR}/guard-action-smoke/out.txt' && grep -q '<!-- pointerverse-guard-comment -->' '${CMAKE_CURRENT_SOURCE_DIR}/scripts/pointerverse-guard-action.sh'"
        )
    endif()
endif()
