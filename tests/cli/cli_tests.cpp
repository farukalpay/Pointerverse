// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

#include "pv/cli/script.hpp"

using namespace pv;

namespace {

std::string shell_quote(const std::filesystem::path& path) {
    return "\"" + path.string() + "\"";
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    return std::string{std::istreambuf_iterator<char>{input}, {}};
}

}  // namespace

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

TEST_CASE("CLI exposes world surface and pack registry commands") {
    const auto repo_dir = std::filesystem::temp_directory_path() / "pointerverse_cli_world_surface";
    std::filesystem::remove_all(repo_dir);
    std::filesystem::create_directories(repo_dir);

    const auto source_root = std::filesystem::path{__FILE__}.parent_path().parent_path().parent_path();
    const auto city_script = source_root / "examples" / "packs" / "city" / "world.pv";
    const auto world_report = repo_dir / "world.txt";
    const auto repl_report = repo_dir / "repl.txt";
    const auto surfaces_report = repo_dir / "surfaces.txt";
    const auto surface_report = repo_dir / "surface.txt";
    const auto packs_report = repo_dir / "packs.txt";
    const auto city_pack_report = repo_dir / "city-pack.txt";
    const auto city_store = repo_dir / "city-store";

    REQUIRE(std::system((shell_quote(POINTERVERSE_CLI_PATH)
        + " world run " + shell_quote(city_script)
        + " > " + shell_quote(world_report)).c_str()) == 0);
    REQUIRE(read_file(world_report).find("World(city)") != std::string::npos);

    REQUIRE(std::system(("printf 'exit\\n' | "
        + shell_quote(POINTERVERSE_CLI_PATH)
        + " world repl > " + shell_quote(repl_report)).c_str()) == 0);
    REQUIRE(read_file(repl_report).find("Pointerverse world terminal") != std::string::npos);

    REQUIRE(std::system((shell_quote(POINTERVERSE_CLI_PATH)
        + " surfaces > " + shell_quote(surfaces_report)).c_str()) == 0);
    REQUIRE(read_file(surfaces_report).find("world") != std::string::npos);

    REQUIRE(std::system((shell_quote(POINTERVERSE_CLI_PATH)
        + " surface show guard > " + shell_quote(surface_report)).c_str()) == 0);
    REQUIRE(read_file(surface_report).find("Turn code diffs into replayable graph evidence") != std::string::npos);

    REQUIRE(std::system((shell_quote(POINTERVERSE_CLI_PATH)
        + " packs > " + shell_quote(packs_report)).c_str()) == 0);
    REQUIRE(read_file(packs_report).find("city") != std::string::npos);

    const auto pack_command =
        "POINTERVERSE_BIN=" + shell_quote(POINTERVERSE_CLI_PATH)
        + " POINTERVERSE_PACK_STORE=" + shell_quote(city_store)
        + " " + shell_quote(POINTERVERSE_CLI_PATH)
        + " pack run city > " + shell_quote(city_pack_report);
    REQUIRE(std::system(pack_command.c_str()) == 0);
    const auto city_output = read_file(city_pack_report);
    REQUIRE(city_output.find("Forked blackout from main") != std::string::npos);
    REQUIRE(city_output.find("Pointerverse Sentinel Boot") != std::string::npos);

    std::filesystem::remove_all(repo_dir);
}

TEST_CASE("CLI trace replay and verify reconstruct exported history") {
    World world;
    cli::ScriptEngine engine{world};
    const auto trace_path = std::filesystem::temp_directory_path() / "pointerverse_cli_replay_trace.jsonl";
    const auto replay_report = std::filesystem::temp_directory_path() / "pointerverse_cli_replay_report.txt";
    const auto verify_report = std::filesystem::temp_directory_path() / "pointerverse_cli_verify_report.txt";
    const auto verify256_report = std::filesystem::temp_directory_path() / "pointerverse_cli_verify256_report.txt";

    std::ostringstream output;
    std::istringstream input{
        "world new seed\n"
        "object A : Node\n"
        "object B : Node\n"
        "link A -> B : causes weight=0.7\n"
        "evolve 1\n"
        "trace export " + trace_path.string() + "\n"
    };
    REQUIRE(engine.run_stream(input, output));

    const auto replay_command = shell_quote(POINTERVERSE_CLI_PATH)
        + " trace replay " + shell_quote(trace_path)
        + " > " + shell_quote(replay_report);
    REQUIRE(std::system(replay_command.c_str()) == 0);
    const auto replay_output = read_file(replay_report);
    REQUIRE(replay_output.find("Replay report") != std::string::npos);
    REQUIRE(replay_output.find("status:           deterministic") != std::string::npos);

    const auto verify_command = shell_quote(POINTERVERSE_CLI_PATH)
        + " trace verify " + shell_quote(trace_path)
        + " --expect-hash " + fmt::format("0x{:016x}", world.hash())
        + " > " + shell_quote(verify_report);
    REQUIRE(std::system(verify_command.c_str()) == 0);
    const auto verify_output = read_file(verify_report);
    REQUIRE(verify_output.find("status:           verified") != std::string::npos);

    const auto verify256_command = shell_quote(POINTERVERSE_CLI_PATH)
        + " trace verify " + shell_quote(trace_path)
        + " --expect-hash " + to_hex(world.canonical_hash())
        + " --expect-commits 4"
        + " --expect-branch main"
        + " > " + shell_quote(verify256_report);
    REQUIRE(std::system(verify256_command.c_str()) == 0);
    const auto verify256_output = read_file(verify256_report);
    REQUIRE(verify256_output.find("commits replayed: 4") != std::string::npos);
    REQUIRE(verify256_output.find("branch:           main") != std::string::npos);
    REQUIRE(verify256_output.find("status:           verified") != std::string::npos);
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

TEST_CASE("CLI repo commands persist replayed trace and verify fsck") {
    const auto repo_dir = std::filesystem::temp_directory_path() / "pointerverse_cli_repo";
    std::filesystem::remove_all(repo_dir);
    std::filesystem::create_directories(repo_dir);

    const auto trace_path = repo_dir / "repo_trace.jsonl";
    const auto report_path = repo_dir / "repo_report.txt";

    World world;
    cli::ScriptEngine engine{world};
    std::ostringstream output;
    std::istringstream input{
        "world new seed\n"
        "object A : Node\n"
        "object B : Node\n"
        "link A -> B : causes weight=0.7\n"
        "evolve 1\n"
        "trace export " + trace_path.string() + "\n"
    };
    REQUIRE(engine.run_stream(input, output));

    const auto command =
        "cd " + shell_quote(repo_dir)
        + " && " + shell_quote(POINTERVERSE_CLI_PATH) + " repo init .pvstore > " + shell_quote(report_path)
        + " && " + shell_quote(POINTERVERSE_CLI_PATH) + " repo commit " + shell_quote(trace_path) + " >> " + shell_quote(report_path)
        + " && " + shell_quote(POINTERVERSE_CLI_PATH) + " repo branch list >> " + shell_quote(report_path)
        + " && " + shell_quote(POINTERVERSE_CLI_PATH) + " repo history main >> " + shell_quote(report_path)
        + " && " + shell_quote(POINTERVERSE_CLI_PATH) + " repo fsck >> " + shell_quote(report_path);

    REQUIRE(std::system(command.c_str()) == 0);
    const auto report = read_file(report_path);
    REQUIRE(report.find("main epoch") != std::string::npos);
    REQUIRE(report.find("replay epoch") != std::string::npos);
    REQUIRE(report.find("status:             clean") != std::string::npos);

    std::filesystem::remove_all(repo_dir);
}

TEST_CASE("CLI sentinel commands boot patrol report and fault demo") {
    const auto repo_dir = std::filesystem::temp_directory_path() / "pointerverse_cli_sentinel";
    std::filesystem::remove_all(repo_dir);
    std::filesystem::create_directories(repo_dir);

    const auto source_root = std::filesystem::path{__FILE__}.parent_path().parent_path().parent_path();
    const auto script = source_root / "examples" / "minimal_reality.pv";
    const auto demo = source_root / "examples" / "packs" / "kernel_corruption" / "run.sh";
    const auto report_path = repo_dir / "sentinel_report.txt";
    const auto demo_report_path = repo_dir / "sentinel_demo_report.txt";

    const auto command =
        "cd " + shell_quote(repo_dir)
        + " && " + shell_quote(POINTERVERSE_CLI_PATH) + " repo init .pvstore > " + shell_quote(report_path)
        + " && " + shell_quote(POINTERVERSE_CLI_PATH) + " repo run " + shell_quote(script) + " --branch main >> " + shell_quote(report_path)
        + " && " + shell_quote(POINTERVERSE_CLI_PATH) + " sentinel boot .pvstore >> " + shell_quote(report_path)
        + " && " + shell_quote(POINTERVERSE_CLI_PATH) + " sentinel patrol .pvstore --once >> " + shell_quote(report_path)
        + " && " + shell_quote(POINTERVERSE_CLI_PATH) + " sentinel report .pvstore >> " + shell_quote(report_path);

    REQUIRE(std::system(command.c_str()) == 0);
    const auto report = read_file(report_path);
    REQUIRE(report.find("Pointerverse Sentinel Boot") != std::string::npos);
    REQUIRE(report.find("boot:              clean") != std::string::npos);
    REQUIRE(report.find("worker heartbeats: clean") != std::string::npos);

    const auto demo_command =
        "POINTERVERSE_BIN=" + shell_quote(POINTERVERSE_CLI_PATH)
        + " " + shell_quote(demo)
        + " > " + shell_quote(demo_report_path)
        + " 2>&1";

    REQUIRE(std::system(demo_command.c_str()) == 0);
    const auto demo_report = read_file(demo_report_path);
    REQUIRE(demo_report.find("detected the injected proof mismatch") != std::string::npos);

    std::filesystem::remove_all(repo_dir);
}

TEST_CASE("CLI repo run supports audit query and why commands") {
    const auto repo_dir = std::filesystem::temp_directory_path() / "pointerverse_cli_repo_run";
    std::filesystem::remove_all(repo_dir);
    std::filesystem::create_directories(repo_dir);

    const auto source_root = std::filesystem::path{__FILE__}.parent_path().parent_path().parent_path();
    const auto script = source_root / "examples" / "agent_audit_valid.pv";
    const auto report_path = repo_dir / "repo_run_report.txt";

    const auto command =
        "cd " + shell_quote(repo_dir)
        + " && " + shell_quote(POINTERVERSE_CLI_PATH) + " repo init .pvstore > " + shell_quote(report_path)
        + " && " + shell_quote(POINTERVERSE_CLI_PATH) + " repo run " + shell_quote(script) + " --branch main >> " + shell_quote(report_path)
        + " && " + shell_quote(POINTERVERSE_CLI_PATH) + " repo query main objects type Agent >> " + shell_quote(report_path)
        + " && " + shell_quote(POINTERVERSE_CLI_PATH) + " repo why main Agent0 modifies FileA >> " + shell_quote(report_path)
        + " && " + shell_quote(POINTERVERSE_CLI_PATH) + " repo fsck >> " + shell_quote(report_path);

    REQUIRE(std::system(command.c_str()) == 0);
    const auto report = read_file(report_path);
    REQUIRE(report.find("=> domain agent_audit loaded") != std::string::npos);
    REQUIRE(report.find("Agent0") != std::string::npos);
    REQUIRE(report.find("why Agent0 modifies FileA") != std::string::npos);
    REQUIRE(report.find("status:             clean") != std::string::npos);

    std::filesystem::remove_all(repo_dir);
}

TEST_CASE("CLI ingest and audit commands produce reports") {
    const auto repo_dir = std::filesystem::temp_directory_path() / "pointerverse_cli_ingest_audit";
    std::filesystem::remove_all(repo_dir);
    std::filesystem::create_directories(repo_dir);

    const auto events_path = repo_dir / "events.jsonl";
    const auto report_path = repo_dir / "audit_report.txt";
    {
        std::ofstream output(events_path);
        output
            << "{\"id\":\"1\",\"agent\":\"Agent0\",\"event\":\"read_file\",\"path\":\"src/main.cpp\",\"ts\":1710000000}\n"
            << "{\"id\":\"2\",\"agent\":\"Agent0\",\"event\":\"write_file\",\"path\":\"src/main.cpp\",\"ts\":1710000001}\n"
            << "{\"id\":\"3\",\"agent\":\"Agent0\",\"event\":\"create_pr\",\"pr\":\"PR42\",\"ts\":1710000002}\n";
    }

    const auto command =
        "cd " + shell_quote(repo_dir)
        + " && " + shell_quote(POINTERVERSE_CLI_PATH) + " repo init .pvstore > " + shell_quote(report_path)
        + " && " + shell_quote(POINTERVERSE_CLI_PATH)
        + " ingest agent-log " + shell_quote(events_path)
        + " --branch main --mode observe --store .pvstore >> " + shell_quote(report_path)
        + " && " + shell_quote(POINTERVERSE_CLI_PATH)
        + " audit report main --format text --store .pvstore >> " + shell_quote(report_path)
        + " && " + shell_quote(POINTERVERSE_CLI_PATH)
        + " audit export main --format json --store .pvstore >> " + shell_quote(report_path)
        + " && " + shell_quote(POINTERVERSE_CLI_PATH)
        + " audit timeline main Agent0 --store .pvstore >> " + shell_quote(report_path)
        + " && " + shell_quote(POINTERVERSE_CLI_PATH) + " repo fsck >> " + shell_quote(report_path);

    REQUIRE(std::system(command.c_str()) == 0);
    const auto report = read_file(report_path);
    REQUIRE(report.find("Ingestion report") != std::string::npos);
    REQUIRE(report.find("accepted:             3") != std::string::npos);
    REQUIRE(report.find("violations: 1") != std::string::npos);
    REQUIRE(report.find("no_pr_without_tests") != std::string::npos);
    REQUIRE(report.find("\"violations\"") != std::string::npos);
    REQUIRE(report.find("Audit timeline: main Agent0") != std::string::npos);
    REQUIRE(report.find("status:             clean") != std::string::npos);

    std::filesystem::remove_all(repo_dir);
}

TEST_CASE("CLI guard run audits PR demo and enforces strict mode") {
    const auto repo_dir = std::filesystem::temp_directory_path() / "pointerverse_cli_guard";
    std::filesystem::remove_all(repo_dir);
    std::filesystem::create_directories(repo_dir);

    const auto source_root = std::filesystem::path{__FILE__}.parent_path().parent_path().parent_path();
    const auto demo_after = source_root / "examples" / "packs" / "code_review" / "after";
    const auto report_path = repo_dir / "audit-report.md";
    const auto sarif_path = repo_dir / "audit.sarif";
    const auto strict_report_path = repo_dir / "strict.json";
    const auto store_path = repo_dir / "pvstore";
    const auto strict_store_path = repo_dir / "strict-pvstore";
    const auto multi_markdown_path = repo_dir / "multi-report.md";
    const auto multi_json_path = repo_dir / "multi-report.json";
    const auto multi_sarif_path = repo_dir / "multi-report.sarif";
    const auto multi_store_path = repo_dir / "multi-pvstore";

    const auto observe_command =
        shell_quote(POINTERVERSE_CLI_PATH)
        + " guard run --repo " + shell_quote(demo_after)
        + " --base ../before --mode observe --format markdown"
        + " --out " + shell_quote(report_path)
        + " --store " + shell_quote(store_path);
    REQUIRE(std::system(observe_command.c_str()) == 0);
    const auto report = read_file(report_path);
    REQUIRE(report.find("Pointerverse Guard") != std::string::npos);
    REQUIRE(report.find("modified_source_requires_test") != std::string::npos);
    REQUIRE(report.find("secret_pattern_in_diff_is_critical") != std::string::npos);

    const auto sarif_command =
        shell_quote(POINTERVERSE_CLI_PATH)
        + " guard run --repo " + shell_quote(demo_after)
        + " --base ../before --mode observe --format sarif"
        + " --out " + shell_quote(sarif_path)
        + " --store " + shell_quote(store_path);
    REQUIRE(std::system(sarif_command.c_str()) == 0);
    REQUIRE(read_file(sarif_path).find("\"version\": \"2.1.0\"") != std::string::npos);

    const auto multi_output_command =
        shell_quote(POINTERVERSE_CLI_PATH)
        + " guard run --repo " + shell_quote(demo_after)
        + " --base ../before --mode observe"
        + " --markdown-out " + shell_quote(multi_markdown_path)
        + " --json-out " + shell_quote(multi_json_path)
        + " --sarif-out " + shell_quote(multi_sarif_path)
        + " --store " + shell_quote(multi_store_path);
    REQUIRE(std::system(multi_output_command.c_str()) == 0);
    REQUIRE(read_file(multi_markdown_path).find("### Artifacts") != std::string::npos);
    REQUIRE(read_file(multi_markdown_path).find("multi-report.sarif") != std::string::npos);
    REQUIRE(read_file(multi_json_path).find("\"artifacts\"") != std::string::npos);
    REQUIRE(read_file(multi_sarif_path).find("\"ruleId\": \"secret_pattern_in_diff_is_critical\"") != std::string::npos);

    const auto history_report = repo_dir / "history.txt";
    const auto history_command =
        shell_quote(POINTERVERSE_CLI_PATH)
        + " repo --store " + shell_quote(store_path)
        + " history guard > " + shell_quote(history_report);
    REQUIRE(std::system(history_command.c_str()) == 0);
    REQUIRE(read_file(history_report).find("ingest git-diff") != std::string::npos);

    const auto strict_command =
        shell_quote(POINTERVERSE_CLI_PATH)
        + " guard run --repo " + shell_quote(demo_after)
        + " --base ../before --mode strict --format json"
        + " --out " + shell_quote(strict_report_path)
        + " --store " + shell_quote(strict_store_path);
    REQUIRE(std::system(strict_command.c_str()) != 0);
    REQUIRE(read_file(strict_report_path).find("\"status\"") != std::string::npos);

    std::filesystem::remove_all(repo_dir);
}

TEST_CASE("CLI runs Realms empire demo pack") {
    const auto repo_dir = std::filesystem::temp_directory_path() / "pointerverse_cli_realms";
    std::filesystem::remove_all(repo_dir);
    std::filesystem::create_directories(repo_dir);

    const auto source_root = std::filesystem::path{__FILE__}.parent_path().parent_path().parent_path();
    const auto demo_script = source_root / "examples" / "packs" / "empire" / "run.sh";
    const auto store_path = repo_dir / "empire-store";
    const auto report_path = repo_dir / "realms-report.txt";

    const auto command =
        "cd " + shell_quote(source_root)
        + " && POINTERVERSE_BIN=" + shell_quote(POINTERVERSE_CLI_PATH)
        + " POINTERVERSE_PACK_STORE=" + shell_quote(store_path)
        + " " + shell_quote(demo_script)
        + " > " + shell_quote(report_path);

    REQUIRE(std::system(command.c_str()) == 0);
    const auto report = read_file(report_path);
    REQUIRE(report.find("why QuarantineEdict quarantines Harbor") != std::string::npos);
    REQUIRE(report.find("objects\n  RedFever") != std::string::npos);
    REQUIRE(report.find("status: Conflict") != std::string::npos);
    REQUIRE(report.find("status:             clean") != std::string::npos);

    std::filesystem::remove_all(repo_dir);
}
