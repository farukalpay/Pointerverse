// SPDX-License-Identifier: Apache-2.0
#include "pv/cli/script.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <fmt/format.h>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "pv/category/composition.hpp"
#include "pv/observer/observer.hpp"

namespace pv::cli {
namespace {

std::string trim(std::string_view value) {
    auto begin = value.begin();
    auto end = value.end();
    while (begin != end && std::isspace(static_cast<unsigned char>(*begin))) {
        ++begin;
    }
    while (begin != end && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
        --end;
    }
    return std::string{begin, end};
}

std::string strip_comment(const std::string& line) {
    const auto marker = line.find('#');
    return marker == std::string::npos ? line : line.substr(0, marker);
}

std::unordered_map<std::string, std::string> parse_options(std::istream& input) {
    std::unordered_map<std::string, std::string> result;
    std::string token;
    while (input >> token) {
        const auto separator = token.find('=');
        if (separator == std::string::npos) {
            result.emplace(token, "true");
        } else {
            result.emplace(token.substr(0, separator), token.substr(separator + 1));
        }
    }
    return result;
}

double double_option(
    const std::unordered_map<std::string, std::string>& options,
    std::string_view name,
    double fallback) {
    if (const auto iter = options.find(std::string{name}); iter != options.end()) {
        return std::stod(iter->second);
    }
    return fallback;
}

std::string string_option(
    const std::unordered_map<std::string, std::string>& options,
    std::string_view name,
    std::string fallback) {
    if (const auto iter = options.find(std::string{name}); iter != options.end()) {
        return iter->second;
    }
    return fallback;
}

bool print_commit(std::ostream& output, const CommitResult& result) {
    if (!result.accepted) {
        output << "=> rejected\n";
        for (const auto& violation : result.violations) {
            output << fmt::format(
                "=> law.{} {} magnitude={:.12g}: {}\n",
                violation.law,
                to_string(violation.severity),
                violation.magnitude,
                violation.explanation);
        }
        return false;
    }
    return true;
}

}  // namespace

ScriptEngine::ScriptEngine(World& world) : world_(world) {}

bool ScriptEngine::run_stream(std::istream& input, std::ostream& output, bool interactive) {
    bool ok = true;
    std::string line;
    while (true) {
        if (interactive) {
            output << "pv> " << std::flush;
        }
        if (!std::getline(input, line)) {
            break;
        }

        const auto clean = trim(strip_comment(line));
        if (clean.empty()) {
            continue;
        }
        if (clean == "quit" || clean == "exit") {
            break;
        }
        ok = execute_line(clean, output) && ok;
    }
    return ok;
}

bool ScriptEngine::run_file(const std::string& path, std::ostream& output) {
    std::ifstream input(path);
    if (!input) {
        output << fmt::format("=> error: cannot open script '{}'\n", path);
        return false;
    }
    output << fmt::format("Reality script: {}\n", path);
    return run_stream(input, output, false);
}

bool ScriptEngine::execute_line(const std::string& raw_line, std::ostream& output) {
    const auto line = trim(strip_comment(raw_line));
    if (line.empty()) {
        return true;
    }

    try {
        std::istringstream stream(line);
        std::string command;
        stream >> command;

        if (command == "help") {
            output << "=> commands: world new, object, link, morphism, compose, law add, evolve, inspect, trace export\n";
            return true;
        }

        if (command == "world") {
            std::string subcommand;
            std::string name;
            stream >> subcommand >> name;
            if (subcommand != "new" || name.empty()) {
                throw std::invalid_argument("usage: world new NAME");
            }
            world_.reset(name);
            verifier_ = Verifier{};
            morphisms_.clear();
            output << fmt::format("=> world {} epoch=0\n", name);
            return true;
        }

        if (command == "object") {
            std::string name;
            std::string colon;
            std::string type;
            stream >> name >> colon >> type;
            if (name.empty() || colon != ":" || type.empty()) {
                throw std::invalid_argument("usage: object NAME : TYPE");
            }
            const auto result = world_.commit(world_.object_delta(name, type), verifier_);
            if (!print_commit(output, result)) {
                return false;
            }
            output << fmt::format("=> object {} : {}\n", name, type);
            return true;
        }

        if (command == "link") {
            std::string from;
            std::string arrow;
            std::string to;
            std::string colon;
            std::string relation;
            stream >> from >> arrow >> to >> colon >> relation;
            if (from.empty() || arrow != "->" || to.empty() || colon != ":" || relation.empty()) {
                throw std::invalid_argument("usage: link FROM -> TO : RELATION [weight=1.0] [role=Structural]");
            }
            const auto options = parse_options(stream);
            const auto role = causal_role_from_string(string_option(options, "role", string_option(options, "causal_role", "Structural")));
            const auto result = world_.commit(
                world_.link_delta(
                    world_.object_by_name(from),
                    world_.object_by_name(to),
                    relation,
                    double_option(options, "weight", 1.0),
                    role),
                verifier_);
            if (!print_commit(output, result)) {
                return false;
            }
            output << fmt::format("=> pointer {} -> {} : {}\n", from, to, relation);
            return true;
        }

        if (command == "morphism" || command == "morph") {
            std::string name;
            stream >> name;
            if (name == "define") {
                stream >> name;
            }

            std::string colon;
            std::string from_type;
            std::string arrow;
            std::string to_type;
            stream >> colon >> from_type >> arrow >> to_type;
            if (name.empty() || colon != ":" || from_type.empty() || arrow != "->" || to_type.empty()) {
                throw std::invalid_argument("usage: morphism NAME : FROM_TYPE -> TO_TYPE");
            }

            auto morphism = std::make_shared<DefinedMorphism>(
                name,
                MorphismSignature{world_.type_id(from_type), world_.type_id(to_type)});
            morphisms_[name] = morphism;
            output << fmt::format("=> morphism {} : {} -> {}\n", name, from_type, to_type);
            return true;
        }

        if (command == "compose") {
            std::string outer;
            std::string op;
            std::string inner;
            stream >> outer >> op >> inner;
            if (outer.empty() || inner.empty() || (op != "after" && op != "o")) {
                throw std::invalid_argument("usage: compose OUTER after INNER");
            }
            const auto g = morphisms_.find(outer);
            const auto f = morphisms_.find(inner);
            if (g == morphisms_.end() || f == morphisms_.end()) {
                throw std::invalid_argument("compose references an unknown morphism");
            }
            auto result = compose(g->second, f->second);
            if (!result) {
                output << fmt::format("=> invalid: {}\n", to_string(result.error()));
                return false;
            }
            output << fmt::format("=> valid morphism: {}\n", (*result)->name());
            return true;
        }

        if (command == "law") {
            std::string subcommand;
            std::string name;
            stream >> subcommand >> name;
            if (subcommand != "add" || name.empty()) {
                throw std::invalid_argument("usage: law add NAME [tolerance=1e-9]");
            }
            const auto options = parse_options(stream);
            verifier_.add_builtin(name, double_option(options, "tolerance", 1e-9));
            output << fmt::format("=> law {} registered\n", name);
            return true;
        }

        if (command == "evolve") {
            std::size_t steps = 0;
            stream >> steps;
            if (steps == 0) {
                throw std::invalid_argument("usage: evolve STEPS");
            }
            const auto result = world_.evolve(steps, verifier_);
            output << fmt::format(
                "=> evolved {} step(s); epoch={}; rejected={}\n",
                result.completed_steps,
                world_.epoch().value,
                result.rejected_steps);
            for (const auto& status : result.last_law_statuses) {
                output << fmt::format(
                    "=> law.{} status={} magnitude={:.12g}\n",
                    status.law,
                    status.passed ? "stable" : to_string(status.severity),
                    status.magnitude);
            }
            return result.rejected_steps == 0;
        }

        if (command == "inspect") {
            std::string what;
            stream >> what;
            const Observer observer{"lab"};
            if (what == "graph") {
                output << observer.inspect_graph(world_.snapshot()).body;
                return true;
            }
            if (what == "object") {
                std::string name;
                stream >> name;
                if (name.empty()) {
                    throw std::invalid_argument("usage: inspect object NAME");
                }
                output << observer.inspect_object(world_.snapshot(), name).body << '\n';
                return true;
            }
            throw std::invalid_argument("usage: inspect graph | inspect object NAME");
        }

        if (command == "trace") {
            std::string subcommand;
            std::string path;
            stream >> subcommand >> path;
            if (subcommand != "export" || path.empty()) {
                throw std::invalid_argument("usage: trace export PATH");
            }
            std::ofstream file(path);
            if (!file) {
                throw std::runtime_error(fmt::format("cannot write trace '{}'", path));
            }
            file << world_.trace().to_jsonl();
            output << fmt::format("=> trace exported: {}\n", path);
            return true;
        }

        throw std::invalid_argument(fmt::format("unknown command '{}'", command));
    } catch (const std::exception& error) {
        output << fmt::format("=> error: {}\n", error.what());
        return false;
    }
}

}  // namespace pv::cli
