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
#include <vector>

#include "pv/category/composition.hpp"
#include "pv/compiler/script_compiler.hpp"
#include "pv/domain/domain.hpp"
#include "pv/hash/canonical.hpp"
#include "pv/kernel/vm.hpp"
#include "pv/observer/observer.hpp"
#include "pv/storage/repository.hpp"

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

CommitResult result_from_record(const CommitRecord& record) {
    CommitResult result;
    result.accepted = record.accepted;
    result.before_epoch = record.before_epoch;
    result.after_epoch = record.after_epoch;
    result.law_statuses = record.law_statuses;
    result.violations = record.violations;
    result.events = record.events;
    result.world_hash = truncated_u64(record.after_hash);
    result.execution_plan_hash = record.execution_plan_hash;
    result.read_set_hash = record.read_set_hash;
    result.write_set_hash = record.write_set_hash;
    result.proof_hash = record.proof_hash;
    result.proof = record.proof;
    return result;
}

bool print_violations(std::ostream& output, const std::vector<LawViolation>& violations) {
    output << "=> rejected\n";
    for (const auto& violation : violations) {
        output << fmt::format(
            "=> law.{} {} magnitude={:.12g}: {}\n",
            violation.law,
            to_string(violation.severity),
            violation.magnitude,
            violation.explanation);
    }
    return false;
}

bool print_commit(std::ostream& output, const CommitResult& result) {
    if (!result.accepted) {
        return print_violations(output, result.violations);
    }
    return true;
}

bool is_rule_command(std::string_view command) noexcept {
    return command == "rule" || command == "when" || command == "require" || command == "deny";
}

std::vector<std::string> morphism_path_from_events(const std::vector<TraceEvent>& events, std::string_view fallback) {
    std::vector<std::string> path;
    for (const auto& event : events) {
        if (event.event != "morphism.apply") {
            continue;
        }
        const auto iter = event.fields.find("name");
        if (iter != event.fields.end() && iter->second != "id") {
            path.push_back(iter->second);
        }
    }
    if (path.empty() && !fallback.empty()) {
        path.emplace_back(fallback);
    }
    return path;
}

Transaction transaction_from_program(
    const WorldSnapshot& snapshot,
    Program program,
    TransactionOrigin origin,
    std::string label,
    bool allow_empty = false) {
    const auto vm = KernelVm{}.execute(snapshot, program);
    if (!vm.ok) {
        throw std::runtime_error(format_vm_diagnostics(vm.diagnostics));
    }

    Transaction tx;
    tx.origin = origin;
    tx.label = std::move(label);
    tx.program = std::move(program);
    tx.delta = vm.delta;
    tx.allow_empty = allow_empty;
    return tx;
}

EvolveResult evolve_through_sink(TransactionSink& sink, std::size_t steps) {
    EvolveResult result;
    result.requested_steps = steps;
    EvolutionProgram program;

    for (std::size_t index = 0; index < steps; ++index) {
        auto delta = program.step(sink.world().snapshot(), Epoch{sink.world().epoch().value + 1});
        delta.append_event(TraceEvent{
            {},
            "evolution.step",
            {{"step", std::to_string(index + 1)}},
            {}
        });

        auto tx = transaction_from_program(
            sink.world().snapshot(),
            ScriptCompiler{}.compile_delta(sink.world().snapshot(), delta),
            TransactionOrigin::Evolution,
            fmt::format("evolve step {}", index + 1),
            true);
        const auto commit = sink.commit(std::move(tx));
        result.last_law_statuses = commit.law_statuses;
        if (commit.accepted) {
            result.completed_steps += 1;
        } else {
            result.rejected_steps += 1;
        }
    }

    return result;
}

std::string read_file(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error(fmt::format("cannot open '{}'", path));
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

}  // namespace

WorldTransactionSink::WorldTransactionSink(World& world, Verifier& verifier)
    : world_(world), verifier_(verifier) {}

World& WorldTransactionSink::world() {
    return world_;
}

const World& WorldTransactionSink::world() const {
    return world_;
}

CommitResult WorldTransactionSink::commit(Transaction tx) {
    auto prepared = prepare_transaction(world_, tx, verifier_);
    return commit_prepared(world_, prepared);
}

bool WorldTransactionSink::reset_world(std::string_view name) {
    world_.reset(std::string{name});
    return true;
}

RepositoryTransactionSink::RepositoryTransactionSink(Repository& repository, std::string branch, Verifier& verifier)
    : repository_(repository), branch_(std::move(branch)), verifier_(verifier) {}

World& RepositoryTransactionSink::world() {
    return repository_.mutable_world(branch_);
}

const World& RepositoryTransactionSink::world() const {
    return repository_.world(branch_);
}

CommitResult RepositoryTransactionSink::commit(Transaction tx) {
    auto record = repository_.commit(branch_, std::move(tx), verifier_);
    if (!record.has_value()) {
        throw std::runtime_error("repository rejected commit before verification");
    }
    return result_from_record(*record);
}

bool RepositoryTransactionSink::reset_world(std::string_view name) {
    const auto& current = repository_.world(branch_);
    return current.epoch().value == 0 && current.objects().empty() && current.name() == name;
}

ScriptEngine::ScriptEngine(World& world)
    : domains_(DomainRegistry::with_builtins()),
      sink_(std::make_unique<WorldTransactionSink>(world, verifier_)) {}

ScriptEngine::ScriptEngine(Repository& repository, std::string branch)
    : domains_(DomainRegistry::with_builtins()),
      sink_(std::make_unique<RepositoryTransactionSink>(repository, std::move(branch), verifier_)) {}

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
    output << fmt::format("World script: {}\n", path);
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
        auto& world = sink_->world();

        if (is_rule_command(command)) {
            const auto completed = rule_builder_.consume_line(line);
            if (completed.has_value()) {
                rule_engine_.add(*completed);
                output << fmt::format("=> rule {} defined\n", completed->name);
            } else if (command == "rule") {
                output << fmt::format("=> rule {} started\n", rule_builder_.name());
            }
            return true;
        }

        if (command == "help") {
            output << "=> commands: world new, domain use, domain load, rule, object, link, morphism, compose, apply, law add, evolve, inspect, trace export\n";
            return true;
        }

        if (command == "world") {
            std::string subcommand;
            std::string name;
            stream >> subcommand >> name;
            if (subcommand != "new" || name.empty()) {
                throw std::invalid_argument("usage: world new NAME");
            }
            if (!sink_->reset_world(name)) {
                throw std::invalid_argument("world new cannot reset a persistent branch after initialization");
            }
            verifier_ = Verifier{};
            rule_engine_ = RuleEngine{};
            rule_builder_.reset();
            morphisms_.clear();
            output << fmt::format("=> world {} epoch=0\n", name);
            return true;
        }

        if (command == "domain") {
            std::string subcommand;
            std::string name_or_path;
            stream >> subcommand >> name_or_path;
            if (subcommand == "use" && !name_or_path.empty()) {
                const auto package = domains_.find(name_or_path);
                if (!package.has_value()) {
                    throw std::invalid_argument(fmt::format("unknown domain '{}'", name_or_path));
                }
                install_domain_schema(world, *package);
                rule_engine_.add_all(package->rules);
                output << fmt::format(
                    "=> domain {} loaded types={} relations={} rules={}\n",
                    package->name,
                    package->schema.object_types.size(),
                    package->schema.relations.size(),
                    package->rules.size());
                return true;
            }
            if (subcommand == "load" && !name_or_path.empty()) {
                const auto rules = parse_rules(read_file(name_or_path));
                const auto count = rules.size();
                rule_engine_.add_all(rules);
                output << fmt::format("=> domain file {} loaded rules={}\n", name_or_path, count);
                return true;
            }
            throw std::invalid_argument("usage: domain use NAME | domain load PATH");
        }

        if (command == "object") {
            std::string name;
            std::string colon;
            std::string type;
            stream >> name >> colon >> type;
            if (name.empty() || colon != ":" || type.empty()) {
                throw std::invalid_argument("usage: object NAME : TYPE");
            }

            auto tx = transaction_from_program(
                world.snapshot(),
                ScriptCompiler{}.compile_object(world.snapshot(), name, type),
                TransactionOrigin::Script,
                fmt::format("object {} : {}", name, type));
            const auto result = sink_->commit(std::move(tx));
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

            auto tx = transaction_from_program(
                world.snapshot(),
                ScriptCompiler{}.compile_link(world.snapshot(), from, to, relation, double_option(options, "weight", 1.0), role),
                TransactionOrigin::Script,
                fmt::format("link {} -> {} : {}", from, to, relation));
            const auto result = sink_->commit(std::move(tx));
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
                MorphismSignature{world.type_id(from_type), world.type_id(to_type)});
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
            morphisms_[std::string{(*result)->name()}] = *result;
            output << fmt::format("=> valid morphism: {}\n", (*result)->name());
            return true;
        }

        if (command == "apply") {
            std::string first;
            stream >> first;
            if (first.empty()) {
                throw std::invalid_argument("usage: apply MORPHISM OBJECT | apply composed OUTER after INNER OBJECT");
            }

            std::shared_ptr<const Morphism> morphism;
            std::string label_name;
            std::string object_name;
            if (first == "composed") {
                std::string outer;
                std::string op;
                std::string inner;
                stream >> outer >> op >> inner >> object_name;
                if (outer.empty() || inner.empty() || object_name.empty() || (op != "after" && op != "o")) {
                    throw std::invalid_argument("usage: apply composed OUTER after INNER OBJECT");
                }
                const auto g = morphisms_.find(outer);
                const auto f = morphisms_.find(inner);
                if (g == morphisms_.end() || f == morphisms_.end()) {
                    throw std::invalid_argument("apply composed references an unknown morphism");
                }
                auto composed = compose(g->second, f->second);
                if (!composed) {
                    output << fmt::format("=> invalid: {}\n", to_string(composed.error()));
                    return false;
                }
                morphism = *composed;
                label_name = fmt::format("{} after {}", outer, inner);
            } else {
                stream >> object_name;
                const auto iter = morphisms_.find(first);
                if (iter == morphisms_.end()) {
                    throw std::invalid_argument("apply references an unknown morphism");
                }
                if (object_name.empty()) {
                    throw std::invalid_argument("usage: apply MORPHISM OBJECT");
                }
                morphism = iter->second;
                label_name = first;
            }

            const Selection selection{{world.object_by_name(object_name)}, {}};
            const auto snapshot = world.snapshot();
            auto delta = morphism->apply(snapshot, selection);
            const auto path = morphism_path_from_events(delta.events_view(), label_name);

            auto tx = transaction_from_program(
                snapshot,
                ScriptCompiler{}.compile_delta(snapshot, delta),
                TransactionOrigin::Morphism,
                fmt::format("apply {} to {}", label_name, object_name),
                true);
            tx.morphism_path = path;
            const auto result = sink_->commit(std::move(tx));
            if (!print_commit(output, result)) {
                return false;
            }
            output << fmt::format("=> applied {} to {}\n", label_name, object_name);
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
            if (rule_engine_.contains(name)) {
                verifier_.add(rule_engine_.make_law(name));
            } else {
                verifier_.add_builtin(name, double_option(options, "tolerance", 1e-9));
            }
            output << fmt::format("=> law {} registered\n", name);

            Delta empty;
            const auto snapshot = world.snapshot();
            const auto validation = verifier_.check(LawCheckContext{snapshot, empty, snapshot});
            if (!validation.accepted) {
                return print_violations(output, validation.violations);
            }
            return true;
        }

        if (command == "evolve") {
            std::size_t steps = 0;
            stream >> steps;
            if (steps == 0) {
                throw std::invalid_argument("usage: evolve STEPS");
            }
            const auto result = evolve_through_sink(*sink_, steps);
            output << fmt::format(
                "=> evolved {} step(s); epoch={}; rejected={}\n",
                result.completed_steps,
                world.epoch().value,
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
            const Observer observer{"world"};
            if (what == "graph") {
                output << observer.inspect_graph(world.snapshot()).body;
                return true;
            }
            if (what == "object") {
                std::string name;
                stream >> name;
                if (name.empty()) {
                    throw std::invalid_argument("usage: inspect object NAME");
                }
                output << observer.inspect_object(world.snapshot(), name).body << '\n';
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
            file << world.trace().to_jsonl();
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
