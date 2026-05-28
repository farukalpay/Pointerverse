#include <catch2/catch_test_macros.hpp>

#include <sstream>

#include "pointerverse/script.hpp"
#include "pointerverse/world.hpp"

using pointerverse::ScriptEngine;
using pointerverse::World;

TEST_CASE("script engine runs the basic categorical reality flow") {
    World world;
    ScriptEngine engine{world};

    std::istringstream input{
        "object A : StateNode dim=2\n"
        "object B : StateNode dim=2\n"
        "link A -> B : correlates_with weight=0.8 causality=causal\n"
        "state A = [0.70710678+0i, 0.70710678+0i]\n"
        "morph Stabilize : StateNode -> StateNode effect=stabilize\n"
        "morph Readout : StateNode -> Observation effect=measure\n"
        "compose Readout after Stabilize\n"
        "law normalization tolerance=1e-9\n"
        "law causality\n"
        "evolve 4\n"
        "observe A probabilities\n"
        "analyze\n"
    };
    std::ostringstream output;

    REQUIRE(engine.run_stream(input, output));
    const auto text = output.str();
    REQUIRE(text.find("=> valid morphism") != std::string::npos);
    REQUIRE(text.find("=> evolved 4 step(s)") != std::string::npos);
    REQUIRE(text.find("probability[0]=") != std::string::npos);
    REQUIRE(text.find("=> invariant:") != std::string::npos);
}

TEST_CASE("script engine reports invalid composition") {
    World world;
    ScriptEngine engine{world};
    std::ostringstream output;

    REQUIRE(engine.execute_line("morph Stabilize : StateNode -> StateNode", output));
    REQUIRE(engine.execute_line("morph Forget : RichObject -> SimpleObject", output));
    REQUIRE_FALSE(engine.execute_line("compose Stabilize after Forget", output));
    REQUIRE(output.str().find("=> invalid morphism") != std::string::npos);
}

TEST_CASE("script engine runs pressure and region workflow") {
    World world;
    ScriptEngine engine{world};

    std::istringstream input{
        "seed contradiction count=6\n"
        "law bounded_pressure tolerance=0.25\n"
        "evolve 3\n"
        "inspect world\n"
        "inspect region R1\n"
        "observe lab region R1 pressure\n"
        "analyze regions\n"
        "trace origin R1\n"
    };
    std::ostringstream output;

    REQUIRE(engine.run_stream(input, output));
    const auto text = output.str();
    REQUIRE(text.find("=> seeded contradiction") != std::string::npos);
    REQUIRE(text.find("regions:") != std::string::npos);
    REQUIRE(text.find("region: R1") != std::string::npos);
    REQUIRE(text.find("pressure=") != std::string::npos);
}
