cmake_minimum_required(VERSION 3.24)

project(Pointerverse VERSION 0.1.0 LANGUAGES CXX)

option(POINTERVERSE_BUILD_TESTS "Build Pointerverse tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(fmt CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(CLI11 CONFIG REQUIRED)

add_library(pointerverse_core
    src/analyzer.cpp
    src/model.cpp
    src/observer.cpp
    src/script.cpp
    src/state_vector.cpp
    src/trace.cpp
    src/types.cpp
    src/world.cpp
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

target_compile_features(pointerverse_core PUBLIC cxx_std_20)

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
        tests/analyzer_tests.cpp
        tests/model_tests.cpp
        tests/script_tests.cpp
        tests/state_vector_tests.cpp
        tests/world_tests.cpp
    )
    target_link_libraries(pointerverse_tests
        PRIVATE
            pointerverse_core
            Catch2::Catch2WithMain
    )

    include(Catch)
    catch_discover_tests(pointerverse_tests)
endif()
