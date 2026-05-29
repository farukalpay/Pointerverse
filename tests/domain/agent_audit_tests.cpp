// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "pv/cli/script.hpp"
#include "pv/domain/agent_audit.hpp"
#include "pv/domain/package.hpp"

using namespace pv;

TEST_CASE("domain package file parses schema and rules together") {
    const auto package = parse_domain_package(
        "domain succession\n"
        "type Crown\n"
        "type Claimant\n"
        "relation claims\n"
        "relation bloodline\n"
        "\n"
        "rule no_claim_without_bloodline\n"
        "when link Claimant -> Crown : claims\n"
        "require before link Claimant -> Crown : bloodline\n"
        "deny reason \"{from} claims {to} without bloodline continuity\"\n");

    REQUIRE(package.name == "succession");
    REQUIRE(package.schema.object_types.size() == 2);
    REQUIRE(package.schema.relations.size() == 2);
    REQUIRE(package.rules.size() == 1);
    REQUIRE(package.rules.front().name == "no_claim_without_bloodline");
}

TEST_CASE("rule-only domain files parse to an empty schema") {
    const auto package = parse_domain_package(
        "rule no_rebellion_without_grievance\n"
        "when link House -> Crown : rebels\n"
        "require before link House -> Crown : grievance\n"
        "deny reason \"{from} rebels against {to} without recorded grievance\"\n");

    REQUIRE(package.name.empty());
    REQUIRE(package.schema.object_types.empty());
    REQUIRE(package.schema.relations.empty());
    REQUIRE(package.rules.size() == 1);
}

TEST_CASE("agent audit domain exposes schema and built-in rule package") {
    const auto package = make_agent_audit_domain();

    REQUIRE(package.name == "agent_audit");
    REQUIRE(package.schema.object_types.size() == 11);
    REQUIRE(package.schema.relations.size() == 14);
    REQUIRE(package.rules.size() == 5);
}

TEST_CASE("agent audit law rejects a write without a read during law registration") {
    World world{"audit"};
    cli::ScriptEngine engine{world};
    std::ostringstream output;
    std::istringstream input{
        "domain use agent_audit\n"
        "object Agent0 : Agent\n"
        "object FileA : File\n"
        "link Agent0 -> FileA : modifies weight=1.0\n"
        "law add no_write_without_read\n"
    };

    REQUIRE_FALSE(engine.run_stream(input, output));
    REQUIRE(output.str().find("=> rejected") != std::string::npos);
    REQUIRE(output.str().find("Agent0 modifies FileA without prior read relation") != std::string::npos);
}

TEST_CASE("domain load imports deterministic rule files") {
    const auto path = std::filesystem::temp_directory_path() / "pointerverse_agent_audit_rules.pvdomain";
    {
        std::ofstream output(path);
        output
            << "rule no_write_without_read\n"
            << "when link Agent -> File : modifies\n"
            << "require exists link Agent -> File : reads\n"
            << "deny reason \"{from} modifies {to} without prior read relation\"\n";
    }

    World world{"audit"};
    cli::ScriptEngine engine{world};
    std::ostringstream output;
    std::istringstream input{
        "object Agent0 : Agent\n"
        "object FileA : File\n"
        "link Agent0 -> FileA : modifies weight=1.0\n"
        "domain load " + path.string() + "\n"
        "law add no_write_without_read\n"
    };

    REQUIRE_FALSE(engine.run_stream(input, output));
    REQUIRE(output.str().find("domain file") != std::string::npos);
    REQUIRE(output.str().find("Agent0 modifies FileA without prior read relation") != std::string::npos);
    std::filesystem::remove(path);
}
