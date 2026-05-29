// SPDX-License-Identifier: Apache-2.0
#include "pv/ingest/graph_log_importer.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <istream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "pv/compiler/program_builder.hpp"
#include "pv/core/relation.hpp"
#include "pv/core/value.hpp"
#include "pv/ingest/evidence.hpp"
#include "pv/ingest/ingestion_index.hpp"
#include "pv/kernel/vm.hpp"
#include "pv/law/verifier.hpp"
#include "pv/rule/rule_engine.hpp"
#include "pv/runtime/transaction.hpp"
#include "pv/storage/repository.hpp"

namespace pv {
namespace {

std::string field(const nlohmann::json& object, std::initializer_list<const char*> names) {
    for (const auto* name : names) {
        const auto iter = object.find(name);
        if (iter != object.end() && iter->is_string()) {
            return iter->get<std::string>();
        }
    }
    return {};
}

bool is_reserved(std::string_view key) {
    static constexpr std::array reserved{
        "id", "event_id", "source", "from", "source_object", "to", "target_object",
        "relation", "rel", "edge", "from_type", "to_type", "weight", "role", "attributes"};
    return std::ranges::find(reserved, key) != reserved.end();
}

Value value_from_json(const nlohmann::json& value) {
    if (value.is_boolean()) {
        return bool_value(value.get<bool>());
    }
    if (value.is_number_unsigned()) {
        return uint64_value(value.get<std::uint64_t>());
    }
    if (value.is_number_integer()) {
        return int64_value(value.get<std::int64_t>());
    }
    if (value.is_number_float()) {
        return float64_value(value.get<double>());
    }
    return string_value(value.get<std::string>());
}

Verifier verifier_for(VerificationMode mode, const std::vector<Rule>& rules) {
    RuleEngine engine;
    engine.add_all(rules);
    Verifier verifier{mode};
    for (const auto& rule : engine.rules()) {
        verifier.add(engine.make_law(rule.name));
    }
    return verifier;
}

GraphEvent graph_event_from_legacy_json(const nlohmann::json& json) {
    if (!json.is_object()) {
        throw std::invalid_argument("graph event line must be a JSON object");
    }

    GraphEvent event;
    event.id = field(json, {"id", "event_id"});
    event.source = "graph-log";
    event.from = field(json, {"from", "source_object"});
    event.to = field(json, {"to", "target_object"});
    event.relation = field(json, {"relation", "rel", "edge"});
    event.from_type = [&] {
        const auto value = field(json, {"from_type"});
        return value.empty() ? std::string{"Node"} : value;
    }();
    event.to_type = [&] {
        const auto value = field(json, {"to_type"});
        return value.empty() ? std::string{"Node"} : value;
    }();
    event.role = field(json, {"role"});
    if (event.role.empty()) {
        event.role = "Structural";
    }
    if (const auto iter = json.find("weight"); iter != json.end() && iter->is_number()) {
        event.weight = iter->get<double>();
    }
    for (const auto& [key, value] : json.items()) {
        if (is_reserved(key) || value.is_null() || value.is_object() || value.is_array()) {
            continue;
        }
        event.attributes.emplace(key, value_from_json(value));
    }
    validate(event);
    return event;
}

void add_message(IngestionResult& result, std::size_t line, const GraphEvent& event, std::string message) {
    result.messages.push_back(IngestionMessage{line, event.id, std::move(message)});
}

void import_event(
    Repository& repository,
    World& world,
    const GraphEvent& event,
    IngestionIndex& index,
    const IngestionOptions& options,
    Verifier& verifier,
    IngestionResult& result,
    std::size_t line_number) {
    validate(event);
    if (!valid_evidence_key(event.source) || !valid_evidence_key(event.id)) {
        throw std::invalid_argument(fmt::format(
            "invalid source '{}' or id '{}' for the import index",
            event.source,
            event.id));
    }
    if (index.seen(event.source, event.id)) {
        result.skipped_duplicates += 1;
        return;
    }

    const auto snapshot = world.snapshot();
    ProgramBuilder builder;
    builder.import_symbols(snapshot);
    const auto from_ref = builder.ensure_object(snapshot, event.from, event.from_type);
    const auto to_ref = builder.ensure_object(snapshot, event.to, event.to_type);
    auto pointer = builder.create_pointer(
        from_ref,
        to_ref,
        event.relation,
        event.weight,
        causal_role_from_string(event.role),
        "core");

    std::vector<std::string> attribute_keys;
    attribute_keys.reserve(event.attributes.size());
    for (const auto& [key, _] : event.attributes) {
        attribute_keys.push_back(key);
    }
    std::ranges::sort(attribute_keys);
    for (const auto& key : attribute_keys) {
        builder.set_pointer_attribute(pointer, key, event.attributes.at(key));
    }

    builder.emit_event(TraceEvent{
        {},
        "graph.event",
        {
            {"source", event.source},
            {"id", event.id},
            {"from", event.from},
            {"relation", event.relation},
            {"to", event.to}
        },
        {}});

    auto program = builder.build();
    const auto vm = KernelVm{}.execute(snapshot, program);
    if (!vm.ok) {
        throw std::runtime_error(format_vm_diagnostics(vm.diagnostics));
    }

    Transaction tx;
    tx.origin = TransactionOrigin::Ingestion;
    tx.label = fmt::format("graph {} {} {} -> {} : {}", event.source, event.id, event.from, event.to, event.relation);
    tx.program = std::move(program);
    tx.delta = vm.delta;

    if (options.mode == VerificationMode::Strict) {
        const auto prepared = prepare_transaction(world, tx, verifier);
        if (!prepared.committable) {
            result.rejected += 1;
            result.violations += prepared.verification.violations.size();
            add_message(
                result,
                line_number,
                event,
                prepared.rejection_reason.empty() ? "transaction rejected" : prepared.rejection_reason);
            return;
        }
    }

    auto record = repository.commit(options.branch, std::move(tx), verifier);
    if (!record.has_value()) {
        result.errors += 1;
        add_message(result, line_number, event, "repository refused to create a commit record");
        return;
    }
    result.violations += record->violations.size();
    if (!record->accepted) {
        result.rejected += 1;
        add_message(result, line_number, event, "transaction rejected");
        return;
    }

    index.mark_seen(event.source, event.id, record->id);
    result.accepted += 1;
}

}  // namespace

GraphLogImporter::GraphLogImporter(Repository& repository) : repository_(repository) {}

IngestionResult GraphLogImporter::import(
    std::istream& input,
    IngestionIndex& index,
    const IngestionOptions& options,
    const std::vector<Rule>& rules) {
    if (options.branch.empty()) {
        throw std::invalid_argument("graph import branch cannot be empty");
    }
    if (!repository_.has_branch(options.branch)) {
        (void)repository_.create_branch(options.branch, World{"graph"});
    }

    auto& world = repository_.mutable_world(options.branch);
    auto verifier = verifier_for(options.mode, rules);

    IngestionResult result;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        line_number += 1;
        if (line.empty()) {
            continue;
        }
        result.events_read += 1;

        try {
            const auto json = nlohmann::json::parse(line);
            const auto event = graph_event_from_legacy_json(json);
            import_event(repository_, world, event, index, options, verifier, result, line_number);
        } catch (const std::exception& error) {
            result.errors += 1;
            result.messages.push_back(IngestionMessage{line_number, {}, error.what()});
        }
    }

    return result;
}

IngestionResult GraphLogImporter::import(
    const std::vector<GraphEvent>& events,
    IngestionIndex& index,
    const IngestionOptions& options,
    const std::vector<Rule>& rules) {
    if (options.branch.empty()) {
        throw std::invalid_argument("graph import branch cannot be empty");
    }
    if (!repository_.has_branch(options.branch)) {
        (void)repository_.create_branch(options.branch, World{"graph"});
    }

    auto& world = repository_.mutable_world(options.branch);
    auto verifier = verifier_for(options.mode, rules);

    IngestionResult result;
    result.events_read = events.size();
    for (const auto& event : events) {
        try {
            import_event(repository_, world, event, index, options, verifier, result, 0);
        } catch (const std::exception& error) {
            result.errors += 1;
            add_message(result, 0, event, error.what());
        }
    }
    return result;
}

}  // namespace pv
