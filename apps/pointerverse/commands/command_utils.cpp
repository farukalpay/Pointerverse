// SPDX-License-Identifier: Apache-2.0
#include "command_utils.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "pv/query/explanation.hpp"
#include "pv/query/query.hpp"

namespace pv::app {

int run_checked(const std::function<int()>& fn) {
    try {
        return fn();
    } catch (const std::exception& error) {
        std::cerr << fmt::format("error: {}\n", error.what());
        return EXIT_FAILURE;
    }
}

std::string read_text_file(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error(fmt::format("cannot open '{}'", path));
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

ExpectedHash parse_expected_hash(const std::string& value) {
    if (auto canonical = parse_hash256(value); canonical.has_value()) {
        return ExpectedHash{canonical, std::nullopt};
    }

    std::size_t consumed = 0;
    const auto base = value.rfind("0x", 0) == 0 || value.rfind("0X", 0) == 0 ? 16 : 10;
    const auto parsed = std::stoull(value, &consumed, base);
    if (consumed != value.size()) {
        throw std::invalid_argument(fmt::format("invalid hash '{}'", value));
    }
    return ExpectedHash{std::nullopt, parsed};
}

bool matches_expected_hash(Hash256 actual, const ExpectedHash& expected) {
    if (expected.canonical.has_value()) {
        return actual == *expected.canonical;
    }
    return expected.legacy.has_value() && truncated_u64(actual) == *expected.legacy;
}

std::string first_world_name(std::string_view jsonl, std::string fallback) {
    std::istringstream input{std::string{jsonl}};
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        try {
            const auto json = nlohmann::json::parse(line);
            const auto fields = json.value("fields", nlohmann::json::object());
            if (!fields.is_object()) {
                continue;
            }
            const auto iter = fields.find("world");
            if (iter != fields.end() && iter->is_string() && !iter->get<std::string>().empty()) {
                return iter->get<std::string>();
            }
        } catch (const std::exception&) {
            return fallback;
        }
    }
    return fallback;
}

std::string first_script_world_name(const std::string& path, std::string fallback) {
    std::ifstream input(path);
    if (!input) {
        return fallback;
    }

    std::string line;
    while (std::getline(input, line)) {
        const auto marker = line.find('#');
        if (marker != std::string::npos) {
            line = line.substr(0, marker);
        }
        std::istringstream stream(line);
        std::string command;
        std::string subcommand;
        std::string name;
        stream >> command >> subcommand >> name;
        if (command == "world" && subcommand == "new" && !name.empty()) {
            return name;
        }
    }
    return fallback;
}

std::string short_hash(CommitId id) {
    return to_hex(id.value).substr(0, 12);
}

void print_replay_report(const ReplayResult& result, std::string_view status) {
    std::cout << "Replay report\n";
    std::cout << "-------------\n";
    std::cout << fmt::format("events read:      {}\n", result.events_read);
    std::cout << fmt::format("events replayed:  {}\n", result.events_replayed);
    std::cout << fmt::format("metadata events:  {}\n", result.metadata_events);
    std::cout << fmt::format("final epoch:      {}\n", result.world.epoch().value);
    std::cout << fmt::format("final hash:       0x{:016x}\n", result.final_hash);
    std::cout << fmt::format("status:           {}\n", status);
    for (const auto& error : result.errors) {
        std::cout << fmt::format("error line {} {}: {}\n", error.line, error.event, error.message);
    }
}

void print_runtime_replay_report(const RuntimeReplayResult& result, std::string_view status) {
    std::cout << "Replay report\n";
    std::cout << "-------------\n";
    std::cout << fmt::format("events read:      {}\n", result.events_read);
    std::cout << fmt::format("events replayed:  {}\n", result.events_replayed);
    std::cout << fmt::format("metadata events:  {}\n", result.metadata_events);
    std::cout << fmt::format("commits replayed: {}\n", result.commits_replayed);
    std::cout << fmt::format("branch:           {}\n", result.branch_name);
    std::cout << fmt::format("final hash:       0x{:016x}\n", truncated_u64(result.final_hash));
    std::cout << fmt::format("final hash256:    {}\n", to_hex(result.final_hash));
    std::cout << fmt::format("status:           {}\n", status);
    for (const auto& error : result.errors) {
        std::cout << fmt::format("error line {} {}: {}\n", error.line, error.event, error.message);
    }
}

void print_integrity_report(const IntegrityReport& report) {
    std::cout << "Repository integrity\n";
    std::cout << "--------------------\n";
    std::cout << fmt::format("objects checked:    {}\n", report.objects_checked);
    std::cout << fmt::format("commits checked:    {}\n", report.commits_checked);
    std::cout << fmt::format("snapshots checked:  {}\n", report.snapshots_checked);
    std::cout << fmt::format("branch refs:        {}\n", report.branch_refs_checked);
    std::cout << fmt::format("status:             {}\n", report.clean() ? "clean" : "errors");
    for (const auto& error : report.errors) {
        std::cout << fmt::format("error: {}\n", error.message);
    }
    for (const auto& warning : report.warnings) {
        std::cout << fmt::format("warning: {}\n", warning.message);
    }
}

namespace {

std::string object_name(const WorldSnapshot& snapshot, ObjectId id) {
    if (const auto* object = snapshot.object(id); object != nullptr) {
        return object->name;
    }
    return to_string(id);
}

const ObjectSnapshot* object_by_name(const WorldSnapshot& snapshot, std::string_view name) {
    for (const auto& object : snapshot.objects) {
        if (object.name == name) {
            return &object;
        }
    }
    return nullptr;
}

bool active_at(const PointerSnapshot& pointer, Epoch epoch) noexcept {
    return pointer.born_at <= epoch && (!pointer.expires_at.has_value() || epoch < *pointer.expires_at);
}

}  // namespace

void print_query_result(const WorldSnapshot& snapshot, const QueryResult& result) {
    if (!result.objects.empty()) {
        std::cout << "objects\n";
        for (const auto& id : result.objects) {
            const auto* object = snapshot.object(id);
            std::cout << fmt::format(
                "  {} {} {}\n",
                object != nullptr ? object->name : to_string(id),
                to_string(id),
                object != nullptr ? snapshot.type_name(object->type) : "");
        }
    }
    if (!result.pointers.empty()) {
        std::cout << "links\n";
        for (const auto& id : result.pointers) {
            const auto* pointer = snapshot.pointer(id);
            if (pointer == nullptr || !active_at(*pointer, snapshot.epoch)) {
                continue;
            }
            std::cout << fmt::format(
                "  {} {} -> {} : {}\n",
                to_string(id),
                object_name(snapshot, pointer->from),
                object_name(snapshot, pointer->to),
                snapshot.relation_name(pointer->relation));
        }
    }
    if (!result.commits.empty()) {
        std::cout << "commits\n";
        for (const auto& id : result.commits) {
            std::cout << fmt::format("  {}\n", short_hash(id));
        }
    }
    if (!result.events.empty()) {
        std::cout << "events\n";
        for (const auto& event : result.events) {
            std::cout << fmt::format("  epoch {} {}\n", event.epoch.value, event.event);
        }
    }
    if (result.objects.empty() && result.pointers.empty() && result.commits.empty() && result.events.empty()) {
        std::cout << "empty\n";
    }
}

QueryResult run_query(const Repository& repository, std::string_view branch, const std::vector<std::string>& terms) {
    if (terms.empty()) {
        throw std::invalid_argument("query cannot be empty");
    }

    const QueryEngine query;
    const RepositoryQueryEngine repository_query;
    const auto snapshot = repository.backend().snapshot(branch);
    if (terms[0] == "objects" && terms.size() == 3 && terms[1] == "type") {
        return repository_query.objects_by_type(repository, branch, terms[2]);
    }
    if (terms[0] == "objects" && terms.size() == 3 && terms[1] == "name") {
        return repository_query.objects_by_name(repository, branch, terms[2]);
    }
    if (terms[0] == "links" && terms.size() == 3 && terms[1] == "relation") {
        return repository_query.links_by_relation(repository, branch, terms[2]);
    }
    if (terms[0] == "commits" && terms.size() == 4 && terms[1] == "touching" && terms[2] == "object") {
        const auto* object = object_by_name(snapshot, terms[3]);
        if (object == nullptr) {
            return {};
        }
        return repository_query.commits_touching_object(repository, branch, object->id);
    }
    if (terms[0] == "events" && terms.size() == 3 && terms[1] == "name") {
        return repository_query.events_by_name(repository, branch, terms[2]);
    }
    if (terms[0] == "cone" && terms.size() >= 3 && terms[1] == "object") {
        const auto* object = object_by_name(snapshot, terms[2]);
        if (object == nullptr) {
            return {};
        }
        std::size_t depth = 1;
        std::string direction = "both";
        for (std::size_t index = 3; index + 1 < terms.size(); index += 2) {
            if (terms[index] == "depth") {
                depth = static_cast<std::size_t>(std::stoull(terms[index + 1]));
            } else if (terms[index] == "direction") {
                direction = terms[index + 1];
            }
        }
        return query.causal_cone(snapshot, object->id, depth, direction);
    }
    throw std::invalid_argument("unsupported query");
}

std::filesystem::path source_root() {
#ifdef POINTERVERSE_SOURCE_ROOT
    return std::filesystem::path{POINTERVERSE_SOURCE_ROOT};
#else
    return std::filesystem::current_path();
#endif
}

std::string shell_quote(const std::filesystem::path& path) {
    auto text = path.string();
    std::string quoted = "'";
    for (const auto ch : text) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

}  // namespace pv::app
