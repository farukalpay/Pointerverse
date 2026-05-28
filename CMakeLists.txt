cmake_minimum_required(VERSION 3.24)

project(Pointerverse VERSION 0.1.0 LANGUAGES CXX)

option(POINTERVERSE_BUILD_TESTS "Build Pointerverse tests" ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(fmt CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(CLI11 CONFIG REQUIRED)

add_library(pointerverse_core
    src/category/composition.cpp
    src/cli/script.cpp
    src/core/id.cpp
    src/core/object.cpp
    src/core/relation.cpp
    src/core/snapshot.cpp
    src/core/type.cpp
    src/core/world.cpp
    src/law/invariant.cpp
    src/law/verifier.cpp
    src/observer/observer.cpp
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
        tests/law/law_tests.cpp
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
