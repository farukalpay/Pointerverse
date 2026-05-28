// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "pv/cli/script.hpp"

using namespace pv;

TEST_CASE("CLI runs minimal M0 graph flow and exports JSONL trace") {
    World world;
    cli::ScriptEngine engine{world};
    const auto trace_path = std::filesystem::temp_directory_path() / "pointerverse_m0_trace.jsonl";

    std::ostringstream output;
    std::istringstream input{
        "world new seed\n"
        "object A : Node\n"
        "object B : Node\n"
        "link A -> B : causes weight=0.7\n"
        "morphism Stabilize : Node -> Node\n"
        "compose Stabilize after Stabilize\n"
        "law add reject_dangling_pointer\n"
        "law add bounded_weight\n"
        "evolve 2\n"
        "inspect graph\n"
        "trace export " + trace_path.string() + "\n"
    };

    REQUIRE(engine.run_stream(input, output));
    REQUIRE(output.str().find("=> valid morphism") != std::string::npos);
    REQUIRE(output.str().find("World(seed) epoch=") != std::string::npos);
    REQUIRE(output.str().find("objects: 2") != std::string::npos);

    std::ifstream trace_file(trace_path);
    REQUIRE(trace_file.good());
    const std::string trace_text{std::istreambuf_iterator<char>{trace_file}, {}};
    REQUIRE(trace_text.find("\"event\":\"object.create\"") != std::string::npos);
    REQUIRE(trace_text.find("\"event\":\"pointer.create\"") != std::string::npos);
    REQUIRE(trace_text.find("\"event\":\"law.check\"") != std::string::npos);
}

TEST_CASE("CLI replay with same commands yields deterministic hash") {
    const auto script =
        "world new seed\n"
        "object A : Node\n"
        "object B : Node\n"
        "link A -> B : causes weight=0.7\n"
        "law add reject_dangling_pointer\n"
        "law add bounded_weight\n"
        "evolve 3\n";

    World left;
    World right;
    cli::ScriptEngine left_engine{left};
    cli::ScriptEngine right_engine{right};
    std::ostringstream left_output;
    std::ostringstream right_output;
    std::istringstream left_input{script};
    std::istringstream right_input{script};

    REQUIRE(left_engine.run_stream(left_input, left_output));
    REQUIRE(right_engine.run_stream(right_input, right_output));
    REQUIRE(left.hash() == right.hash());
}
