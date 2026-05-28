#include "pointerverse/script.hpp"

#include <algorithm>
#include <cctype>
#include <complex>
#include <fstream>
#include <fmt/format.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "pointerverse/analyzer.hpp"
#include "pointerverse/observer.hpp"

namespace pointerverse {
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

    return std::string(begin, end);
}

std::string strip_comment(const std::string& line) {
    const auto marker = line.find('#');
    if (marker == std::string::npos) {
        return line;
    }
    return line.substr(0, marker);
}

std::map<std::string, std::string> parse_key_values(std::istream& input) {
    std::map<std::string, std::string> result;
    std::string token;
    while (input >> token) {
        const auto separator = token.find('=');
        if (separator == std::string::npos) {
            result.emplace(token, "true");
            continue;
        }
        result.emplace(token.substr(0, separator), token.substr(separator + 1));
    }
    return result;
}

double parse_double_option(
    const std::map<std::string, std::string>& values,
    const std::string& key,
    double fallback) {
    const auto iter = values.find(key);
    if (iter == values.end()) {
        return fallback;
    }
    return std::stod(iter->second);
}

std::string parse_string_option(
    const std::map<std::string, std::string>& values,
    const std::string& key,
    const std::string& fallback) {
    const auto iter = values.find(key);
    if (iter == values.end()) {
        return fallback;
    }
    return iter->second;
}

Scalar parse_scalar(std::string token) {
    token.erase(std::remove_if(token.begin(), token.end(), [](unsigned char ch) {
        return std::isspace(ch);
    }), token.end());

    if (token.empty()) {
        throw std::invalid_argument("empty scalar");
    }

    if (token.back() != 'i') {
        return Scalar{std::stod(token), 0.0};
    }

    auto body = token.substr(0, token.size() - 1);
    if (body.empty() || body == "+") {
        return Scalar{0.0, 1.0};
    }
    if (body == "-") {
        return Scalar{0.0, -1.0};
    }

    std::size_t split = std::string::npos;
    for (std::size_t index = 1; index < body.size(); ++index) {
        const auto ch = body[index];
        const auto previous = body[index - 1];
        if ((ch == '+' || ch == '-') && previous != 'e' && previous != 'E') {
            split = index;
        }
    }

    if (split == std::string::npos) {
        return Scalar{0.0, std::stod(body)};
    }

    return Scalar{
        std::stod(body.substr(0, split)),
        std::stod(body.substr(split))
    };
}

StateVector parse_state_vector(const std::string& text) {
    const auto open = text.find('[');
    const auto close = text.rfind(']');
    if (open == std::string::npos || close == std::string::npos || close <= open) {
        throw std::invalid_argument("state vector must use [a, b, c] syntax");
    }

    std::vector<Scalar> amplitudes;
    std::string body = text.substr(open + 1, close - open - 1);
    std::stringstream stream(body);
    std::string token;
    while (std::getline(stream, token, ',')) {
        amplitudes.push_back(parse_scalar(token));
    }

    if (amplitudes.empty()) {
        throw std::invalid_argument("state vector cannot be empty");
    }

    return StateVector{std::move(amplitudes)};
}

void print_law_results(std::ostream& output, const std::vector<LawResult>& results) {
    for (const auto& result : results) {
        output << fmt::format(
            "=> law.{} {} value={:.12g} ({})\n",
            result.name,
            result.passed ? "ok" : "failed",
            result.value,
            result.detail);
    }
}

std::vector<std::string> remaining_tokens(std::istream& stream) {
    std::vector<std::string> tokens;
    std::string token;
    while (stream >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

std::size_t parse_size_option(
    const std::map<std::string, std::string>& values,
    const std::string& key,
    std::size_t fallback) {
    const auto iter = values.find(key);
    if (iter == values.end()) {
        return fallback;
    }
    return static_cast<std::size_t>(std::stoull(iter->second));
}

Observer make_observer(const World& world, const std::string& name) {
    if (!world.has_object(name)) {
        return Observer{name};
    }

    const auto handle = world.object_by_name(name);
    const auto& object = world.object(handle);
    std::size_t scope = 2;
    double resolution = 1.0;
    double bias = 0.0;
    std::size_t memory = 16;

    if (const auto iter = object.config.find("scope"); iter != object.config.end()) {
        scope = static_cast<std::size_t>(std::stoull(iter->second));
    }
    if (const auto iter = object.config.find("resolution"); iter != object.config.end()) {
        resolution = std::stod(iter->second);
    }
    if (const auto iter = object.config.find("bias"); iter != object.config.end()) {
        bias = std::stod(iter->second);
    }
    if (const auto iter = object.config.find("memory"); iter != object.config.end()) {
        memory = static_cast<std::size_t>(std::stoull(iter->second));
    }

    return Observer{name, handle, scope, resolution, bias, memory};
}

void print_observation(std::ostream& output, const Observation& observation) {
    if (observation.denied) {
        output << fmt::format(
            "=> observe {} {}: denied\n=> reason: {}\n",
            observation.target_name,
            observation.quantity,
            observation.denial_reason);
        return;
    }
    output << fmt::format("=> observe {} {}: {}\n", observation.target_name, observation.quantity, observation.summary);
}

void print_analyzer_report(std::ostream& output, const AnalyzerReport& report) {
    for (const auto& invariant : report.invariants) {
        output << fmt::format(
            "=> invariant: {} confidence={:.3f} ({})\n",
            invariant.kind,
            invariant.confidence,
            invariant.message);
    }
    if (report.anomalies.empty()) {
        output << "=> anomaly: none detected\n";
    } else {
        for (const auto& anomaly : report.anomalies) {
            output << fmt::format(
                "=> anomaly: {} confidence={:.3f} ({})\n",
                anomaly.kind,
                anomaly.confidence,
                anomaly.message);
        }
    }
    for (const auto& experiment : report.suggested_experiments) {
        output << fmt::format("=> suggested experiment: {}\n", experiment);
    }
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
        if (clean == "quit" || clean == "exit") {
            break;
        }
        if (clean.empty()) {
            continue;
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
            output << "=> commands: object, link, state, morph, compose, law, seed, external import, apply, evolve, observe, analyze, inspect, trace export, trace origin\n";
            return true;
        }

        if (command == "object") {
            std::string name;
            std::string colon;
            std::string type;
            stream >> name >> colon >> type;
            if (name.empty() || colon != ":" || type.empty()) {
                throw std::invalid_argument("usage: object NAME : TYPE [key=value...]");
            }
            auto config = parse_key_values(stream);
            const auto handle = world_.create_object(type, name, std::move(config));
            (void)handle;
            output << fmt::format("=> object {} : {}\n", name, type);
            return true;
        }

        if (command == "link") {
            std::string source;
            std::string arrow;
            std::string target;
            std::string colon;
            std::string relation;
            stream >> source >> arrow >> target >> colon >> relation;
            if (source.empty() || arrow != "->" || target.empty() || colon != ":" || relation.empty()) {
                throw std::invalid_argument("usage: link SOURCE -> TARGET : RELATION [weight=1.0] [causality=structural]");
            }
            const auto options = parse_key_values(stream);
            const auto weight = parse_double_option(options, "weight", 1.0);
            const auto causality = causal_tag_from_string(parse_string_option(options, "causality", "structural"));
            const auto id = world_.link(
                world_.object_by_name(source),
                world_.object_by_name(target),
                relation,
                weight,
                causality);
            output << fmt::format(
                "=> pointer {} {} -> {} : {} weight={:.6g} causality={}\n",
                to_string(id),
                source,
                target,
                relation,
                weight,
                to_string(causality));
            return true;
        }

        if (command == "state") {
            const auto rest = trim(line.substr(command.size()));
            const auto separator = rest.find('=');
            if (separator == std::string::npos) {
                throw std::invalid_argument("usage: state NAME = [a, b, c]");
            }
            const auto name = trim(std::string_view(rest).substr(0, separator));
            const auto vector_text = trim(std::string_view(rest).substr(separator + 1));
            auto state = parse_state_vector(vector_text);
            world_.set_state(world_.object_by_name(name), std::move(state));
            output << fmt::format("=> state {} = {}\n", name, world_.object(world_.object_by_name(name)).state.summary());
            return true;
        }

        if (command == "morph") {
            std::string name;
            std::string colon;
            std::string from_type;
            std::string arrow;
            std::string to_type;
            stream >> name >> colon >> from_type >> arrow >> to_type;
            if (name.empty() || colon != ":" || from_type.empty() || arrow != "->" || to_type.empty()) {
                throw std::invalid_argument("usage: morph NAME : FROM_TYPE -> TO_TYPE [effect=identity]");
            }
            const auto options = parse_key_values(stream);
            const auto effect = parse_string_option(options, "effect", "identity");
            const auto id = world_.register_morphism(name, from_type, to_type, effect);
            (void)id;
            output << fmt::format("=> morph {} : {} -> {} effect={}\n", name, from_type, to_type, effect);
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
            const auto result = world_.compose(outer, inner);
            if (result.valid) {
                output << fmt::format(
                    "=> valid morphism: {} : {} -> {}\n",
                    result.name,
                    result.from_type,
                    result.to_type);
                if (result.weakly_valid) {
                    output << fmt::format("=> warning: {}\n", result.warnings.front());
                }
            } else {
                output << fmt::format("=> invalid morphism: {}\n", result.errors.front());
            }
            return result.valid;
        }

        if (command == "seed") {
            std::string kind;
            stream >> kind;
            if (kind != "contradiction") {
                throw std::invalid_argument("usage: seed contradiction count=N [prefix=C]");
            }
            const auto options = parse_key_values(stream);
            const auto count = parse_size_option(options, "count", 1);
            auto result = world_.seed_contradiction(count, options);
            output << fmt::format(
                "=> seeded contradiction: objects={} pointers={} max_pressure={:.6f}\n",
                result.objects.size(),
                result.pointers.size(),
                result.pressure.magnitude);
            return true;
        }

        if (command == "external") {
            std::string subcommand;
            std::string path;
            stream >> subcommand >> path;
            if (subcommand != "import" || path.empty()) {
                throw std::invalid_argument("usage: external import PATH");
            }

            std::ifstream input(path);
            if (!input) {
                throw std::runtime_error(fmt::format("cannot open external import '{}'", path));
            }
            const auto json = nlohmann::json::parse(input);
            const auto events = json.at("events");
            if (!events.is_array()) {
                throw std::invalid_argument("external import requires an events array");
            }

            std::size_t object_count = 0;
            std::size_t pointer_count = 0;
            double max_pressure = 0.0;
            for (const auto& event_json : events) {
                const auto event = external_event_from_json(event_json);
                const auto result = world_.ingest_external_event(event);
                object_count += result.objects.size();
                pointer_count += result.pointers.size();
                max_pressure = std::max(max_pressure, result.pressure.magnitude);
            }
            output << fmt::format(
                "=> external import: events={} objects={} pointers={} max_pressure={:.6f}\n",
                events.size(),
                object_count,
                pointer_count,
                max_pressure);
            return true;
        }

        if (command == "apply") {
            std::string morphism_name;
            std::string to_keyword;
            std::string target_name;
            stream >> morphism_name >> to_keyword >> target_name;
            if (morphism_name.empty() || to_keyword != "to" || target_name.empty()) {
                throw std::invalid_argument("usage: apply MORPHISM to TARGET");
            }
            if (!world_.has_object(target_name)) {
                throw std::invalid_argument(fmt::format("unknown object '{}'", target_name));
            }
            if (!world_.has_morphism(morphism_name)) {
                const auto target = world_.object(world_.object_by_name(target_name));
                const auto id = world_.register_morphism(morphism_name, target.type, target.type, morphism_name);
                (void)id;
            }

            const auto result = world_.apply_morphism(morphism_name, target_name);
            output << fmt::format(
                "=> morphism {} {} target={} pressure_delta={:.6f} residual={:.6f} counterfactual_delta={:.6f}\n",
                result.morphism,
                result.valid ? "applied" : "rejected",
                result.target,
                result.pressure_delta,
                result.law_residual,
                result.counterfactual_delta);
            output << fmt::format("=> reason: {}\n", result.reason);
            for (const auto& event : result.events) {
                output << fmt::format("=> event: {}\n", event);
            }
            return true;
        }

        if (command == "law") {
            std::string name;
            stream >> name;
            if (name.empty()) {
                throw std::invalid_argument("usage: law NAME [tolerance=1e-9]");
            }
            const auto options = parse_key_values(stream);
            const auto tolerance = parse_double_option(options, "tolerance", 1e-9);
            world_.add_builtin_law(name, tolerance);
            output << fmt::format("=> law {} registered tolerance={:.12g}\n", name, tolerance);
            return true;
        }

        if (command == "evolve") {
            std::size_t steps = 0;
            stream >> steps;
            if (steps == 0) {
                throw std::invalid_argument("usage: evolve STEPS where STEPS > 0");
            }
            const auto result = world_.evolve(steps);
            output << fmt::format(
                "=> evolved {} step(s); time={}; status={}\n",
                result.completed_steps,
                world_.time(),
                result.passed ? "stable" : "violated");
            for (const auto& event : result.events) {
                output << fmt::format("=> event: {}\n", event);
            }
            output << fmt::format(
                "=> regions formed: {}\n=> max pressure: {:.6f}\n",
                result.regions_formed,
                result.max_pressure);
            print_law_results(output, result.law_results);
            return result.passed;
        }

        if (command == "observe") {
            const auto tokens = remaining_tokens(stream);
            if (tokens.size() == 2) {
                const Observer observer{"lab"};
                const auto observation = observer.observe(world_, world_.object_by_name(tokens[0]), tokens[1]);
                print_observation(output, observation);
                return !observation.denied;
            }
            if (tokens.size() == 4 && tokens[1] == "region") {
                const auto observer = make_observer(world_, tokens[0]);
                const auto observation = observer.observe_region(world_, tokens[2], tokens[3]);
                print_observation(output, observation);
                return !observation.denied;
            }
            if (tokens.size() == 3 && tokens[1] == "world") {
                const auto observer = make_observer(world_, tokens[0]);
                const auto observation = observer.observe_world(world_, tokens[2]);
                print_observation(output, observation);
                return !observation.denied;
            }
            if (tokens.size() == 3) {
                const auto observer = make_observer(world_, tokens[0]);
                const auto observation = observer.observe(world_, world_.object_by_name(tokens[1]), tokens[2]);
                print_observation(output, observation);
                return !observation.denied;
            }
            throw std::invalid_argument("usage: observe TARGET QUANTITY | observe OBSERVER TARGET QUANTITY | observe OBSERVER region REGION QUANTITY | observe OBSERVER world QUANTITY");
        }

        if (command == "analyze") {
            std::string mode;
            stream >> mode;
            const Analyzer analyzer;
            AnalyzerReport report;
            if (mode.empty()) {
                report = analyzer.scan(world_.trace());
            } else if (mode == "regions") {
                report = analyzer.scan_regions(world_.trace());
            } else if (mode == "attractors") {
                report = analyzer.scan_attractors(world_.trace());
            } else if (mode == "law-drift") {
                report = analyzer.scan_law_drift(world_.trace());
            } else if (mode == "noncommutativity") {
                report = analyzer.scan_noncommutativity(world_.trace());
            } else {
                throw std::invalid_argument("usage: analyze [regions|attractors|law-drift|noncommutativity]");
            }
            print_analyzer_report(output, report);
            return true;
        }

        if (command == "inspect") {
            std::string what;
            stream >> what;
            if (what == "world") {
                output << world_.inspect_world() << '\n';
                return true;
            }
            if (what == "region") {
                std::string region_name;
                stream >> region_name;
                if (region_name.empty()) {
                    throw std::invalid_argument("usage: inspect region REGION");
                }
                output << world_.inspect_region(world_.region_by_name(region_name).id) << '\n';
                return true;
            }
            if (what != "graph") {
                throw std::invalid_argument("usage: inspect graph|world|region");
            }
            output << "=> graph\n";
            if (world_.relations().empty()) {
                output << "=> graph is empty\n";
            }
            for (const auto& relation : world_.relations()) {
                const auto& source = world_.object(relation.source);
                const auto& target = world_.object(relation.target);
                output << fmt::format(
                    "{}\n  {}({:.6g}, {}) -> {}\n",
                    source.name,
                    relation.relation,
                    relation.weight,
                    to_string(relation.causality),
                    target.name);
            }
            return true;
        }

        if (command == "trace") {
            std::string subcommand;
            std::string path;
            stream >> subcommand >> path;
            if (subcommand == "origin") {
                if (path.empty()) {
                    throw std::invalid_argument("usage: trace origin REGION");
                }
                output << world_.trace_origin(world_.region_by_name(path).id) << '\n';
                return true;
            }
            if (subcommand != "export" || path.empty()) {
                throw std::invalid_argument("usage: trace export PATH | trace origin REGION");
            }
            std::ofstream output_file(path);
            if (!output_file) {
                throw std::runtime_error(fmt::format("cannot write trace '{}'", path));
            }
            output_file << world_.trace().to_json().dump(2) << '\n';
            output << fmt::format("=> trace exported: {}\n", path);
            return true;
        }

        throw std::invalid_argument(fmt::format("unknown command '{}'", command));
    } catch (const std::exception& error) {
        output << fmt::format("=> error: {}\n", error.what());
        return false;
    }
}

}  // namespace pointerverse
