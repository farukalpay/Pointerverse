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
find_package(tomlplusplus CONFIG REQUIRED)

function(pointerverse_configure_target target)
    target_include_directories(${target}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    )
    target_compile_features(${target} PUBLIC cxx_std_23)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(pointerverse_kernel
    src/category/composition.cpp
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
    src/core/world_index.cpp
    src/hash/canonical.cpp
    src/hash/hasher.cpp
    src/kernel/canonical_codec.cpp
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
    src/runtime/commit_record.cpp
    src/runtime/ids.cpp
    src/trace/recorder.cpp
)
pointerverse_configure_target(pointerverse_kernel)
target_link_libraries(pointerverse_kernel
    PUBLIC
        fmt::fmt
        nlohmann_json::nlohmann_json
        OpenSSL::Crypto
)

add_library(pointerverse_runtime
    src/core/world.cpp
    src/runtime/commit_graph.cpp
    src/runtime/merge.cpp
    src/runtime/replayer.cpp
    src/runtime/snapshot_store.cpp
    src/runtime/transaction.cpp
    src/runtime/world_store.cpp
    src/trace/replayer.cpp
)
pointerverse_configure_target(pointerverse_runtime)
target_link_libraries(pointerverse_runtime PUBLIC pointerverse_kernel)

add_library(pointerverse_storage
    src/storage/commit_index.cpp
    src/storage/content_store.cpp
    src/storage/event_index.cpp
    src/storage/index_store.cpp
    src/storage/integrity.cpp
    src/storage/lock_file.cpp
    src/storage/manifest.cpp
    src/storage/object_codec.cpp
    src/storage/recovery.cpp
    src/storage/ref_store.cpp
    src/storage/repository_engine.cpp
    src/storage/repository.cpp
    src/storage/repository_transaction.cpp
    src/storage/wal.cpp
    src/storage/world_index_store.cpp
)
pointerverse_configure_target(pointerverse_storage)
target_link_libraries(pointerverse_storage PUBLIC pointerverse_runtime)

add_library(pointerverse_sentinel
    src/sentinel/boot_gate.cpp
    src/sentinel/fault_injection.cpp
    src/sentinel/heartbeat.cpp
    src/sentinel/patrol.cpp
    src/sentinel/region_table.cpp
    src/sentinel/report.cpp
    src/sentinel/sentinel.cpp
)
pointerverse_configure_target(pointerverse_sentinel)
target_link_libraries(pointerverse_sentinel PUBLIC pointerverse_storage)

add_library(pointerverse_query
    src/query/explanation.cpp
    src/query/graph_query.cpp
    src/query/history_query.cpp
    src/query/query.cpp
)
pointerverse_configure_target(pointerverse_query)
target_link_libraries(pointerverse_query PUBLIC pointerverse_storage)

add_library(pointerverse_rules
    src/rule/pattern.cpp
    src/rule/rule.cpp
    src/rule/rule_engine.cpp
)
pointerverse_configure_target(pointerverse_rules)
target_link_libraries(pointerverse_rules PUBLIC pointerverse_kernel)

add_library(pointerverse_domains
    src/domain/agent_audit.cpp
    src/domain/domain.cpp
    src/domain/package.cpp
    src/domain/schema.cpp
)
pointerverse_configure_target(pointerverse_domains)
target_link_libraries(pointerverse_domains PUBLIC pointerverse_rules)

add_library(pointerverse_ingest
    src/ingest/agent_audit_adapter.cpp
    src/ingest/evidence.cpp
    src/ingest/event_schema.cpp
    src/ingest/graph_log_importer.cpp
    src/ingest/ingestion_index.cpp
    src/ingest/ingestion_pipeline.cpp
    src/ingest/jsonl_adapter.cpp
    src/ingest/normalizer.cpp
)
pointerverse_configure_target(pointerverse_ingest)
target_link_libraries(pointerverse_ingest PUBLIC pointerverse_storage pointerverse_domains)

add_library(pointerverse_audit
    src/audit/report.cpp
    src/audit/risk_score.cpp
    src/audit/timeline.cpp
    src/audit/violation_report.cpp
)
pointerverse_configure_target(pointerverse_audit)
target_link_libraries(pointerverse_audit PUBLIC pointerverse_storage pointerverse_ingest)

add_library(pointerverse_guard
    src/guard/finding.cpp
    src/guard/git_diff_adapter.cpp
    src/guard/guard_pipeline.cpp
    src/guard/policy_pack.cpp
    src/guard/report.cpp
)
pointerverse_configure_target(pointerverse_guard)
target_link_libraries(pointerverse_guard PUBLIC pointerverse_ingest pointerverse_audit)

add_library(pointerverse_cli_common
    src/cli/script.cpp
    src/cli_app/registry.cpp
    src/surface/manifest.cpp
    src/surface/registry.cpp
)
pointerverse_configure_target(pointerverse_cli_common)
target_link_libraries(pointerverse_cli_common
    PUBLIC
        pointerverse_storage
        pointerverse_query
        pointerverse_domains
        pointerverse_rules
        CLI11::CLI11
        tomlplusplus::tomlplusplus
)

add_library(pointerverse_sdk INTERFACE)
target_link_libraries(pointerverse_sdk
    INTERFACE
        pointerverse_kernel
        pointerverse_runtime
        pointerverse_storage
        pointerverse_sentinel
        pointerverse_query
        pointerverse_rules
        pointerverse_domains
)

add_executable(pointerverse
    apps/pointerverse/main.cpp
    apps/pointerverse/commands/audit_command.cpp
    apps/pointerverse/commands/command_utils.cpp
    apps/pointerverse/commands/commands.cpp
    apps/pointerverse/commands/guard_command.cpp
    apps/pointerverse/commands/ingest_command.cpp
    apps/pointerverse/commands/pack_command.cpp
    apps/pointerverse/commands/pack_runner.cpp
    apps/pointerverse/commands/repo_command.cpp
    apps/pointerverse/commands/sentinel_command.cpp
    apps/pointerverse/commands/surface_command.cpp
    apps/pointerverse/commands/trace_command.cpp
    apps/pointerverse/commands/world_command.cpp
)
target_link_libraries(pointerverse
    PRIVATE
        pointerverse_cli_common
        pointerverse_sentinel
        pointerverse_guard
        pointerverse_audit
        pointerverse_ingest
        fmt::fmt
)
target_compile_definitions(pointerverse
    PRIVATE
        POINTERVERSE_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_executable(pointerverse_kernel_stress examples/packs/kernel_stress/kernel_stress.cpp)
target_link_libraries(pointerverse_kernel_stress
    PRIVATE
        pointerverse_storage
        fmt::fmt
)

if(POINTERVERSE_BUILD_TESTS)
    enable_testing()
    find_package(Catch2 3 CONFIG REQUIRED)

    add_executable(pointerverse_tests
        tests/architecture/target_boundary_tests.cpp
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
        tests/query/repository_query_tests.cpp
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
        tests/storage/lazy_load_tests.cpp
        tests/storage/repository_index_tests.cpp
        tests/storage/repository_lock_tests.cpp
        tests/storage/repository_recovery_tests.cpp
        tests/storage/repository_tests.cpp
        tests/storage/wal_tests.cpp
        tests/trace/replay_tests.cpp
    )
    target_link_libraries(pointerverse_tests
        PRIVATE
            pointerverse_cli_common
            pointerverse_sdk
            pointerverse_sentinel
            pointerverse_ingest
            pointerverse_audit
            pointerverse_guard
            Catch2::Catch2WithMain
    )
    target_compile_definitions(pointerverse_tests
        PRIVATE
            POINTERVERSE_CLI_PATH="$<TARGET_FILE:pointerverse>"
            POINTERVERSE_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
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
                "rm -rf '${CMAKE_CURRENT_BINARY_DIR}/guard-action-smoke' && mkdir -p '${CMAKE_CURRENT_BINARY_DIR}/guard-action-smoke' && POINTERVERSE_BIN='$<TARGET_FILE:pointerverse>' GITHUB_WORKSPACE='${CMAKE_CURRENT_BINARY_DIR}/guard-action-smoke' POINTERVERSE_GUARD_REPO='${CMAKE_CURRENT_SOURCE_DIR}/examples/packs/code_review/after' POINTERVERSE_GUARD_BASE='../before' POINTERVERSE_GUARD_COMMENT='false' '${CMAKE_CURRENT_SOURCE_DIR}/scripts/pointerverse-guard-action.sh' > '${CMAKE_CURRENT_BINARY_DIR}/guard-action-smoke/out.txt' && grep -q '::error file=config/dev.env' '${CMAKE_CURRENT_BINARY_DIR}/guard-action-smoke/out.txt' && grep -q '<!-- pointerverse-guard-comment -->' '${CMAKE_CURRENT_SOURCE_DIR}/scripts/pointerverse-guard-action.sh'"
        )
    endif()
endif()
