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
        "id", "event_id", "from", "source_object", "to", "target_object",
        "relation", "rel", "edge", "from_type", "to_type", "weight", "role"};
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
            if (!json.is_object()) {
                throw std::invalid_argument("graph event line must be a JSON object");
            }

            const auto id = field(json, {"id", "event_id"});
            const auto from = field(json, {"from", "source_object"});
            const auto to = field(json, {"to", "target_object"});
            const auto relation = field(json, {"relation", "rel", "edge"});
            if (id.empty() || from.empty() || to.empty() || relation.empty()) {
                throw std::invalid_argument("graph event requires id, from, to, and relation");
            }
            if (!valid_evidence_key(id)) {
                throw std::invalid_argument(fmt::format("invalid id '{}' for the import index", id));
            }
            if (index.seen("graph-log", id)) {
                result.skipped_duplicates += 1;
                continue;
            }

            const auto from_type = [&] {
                const auto value = field(json, {"from_type"});
                return value.empty() ? std::string{"Node"} : value;
            }();
            const auto to_type = [&] {
                const auto value = field(json, {"to_type"});
                return value.empty() ? std::string{"Node"} : value;
            }();
            double weight = 1.0;
            if (const auto iter = json.find("weight"); iter != json.end() && iter->is_number()) {
                weight = iter->get<double>();
            }
            const auto role = causal_role_from_string(field(json, {"role"}));

            const auto snapshot = world.snapshot();
            ProgramBuilder builder;
            builder.import_symbols(snapshot);
            const auto from_ref = builder.ensure_object(snapshot, from, from_type);
            const auto to_ref = builder.ensure_object(snapshot, to, to_type);
            auto pointer = builder.create_pointer(from_ref, to_ref, relation, weight, role, "core");
            for (const auto& [key, value] : json.items()) {
                if (is_reserved(key) || value.is_null() || value.is_object() || value.is_array()) {
                    continue;
                }
                builder.set_pointer_attribute(pointer, key, value_from_json(value));
            }
            builder.emit_event(TraceEvent{
                {},
                "graph.event",
                {{"id", id}, {"from", from}, {"relation", relation}, {"to", to}},
                {}});

            auto program = builder.build();
            const auto vm = KernelVm{}.execute(snapshot, program);
            if (!vm.ok) {
                throw std::runtime_error(format_vm_diagnostics(vm.diagnostics));
            }

            Transaction tx;
            tx.origin = TransactionOrigin::Ingestion;
            tx.label = fmt::format("graph {} {} -> {} : {}", id, from, to, relation);
            tx.program = std::move(program);
            tx.delta = vm.delta;

            if (options.mode == VerificationMode::Strict) {
                const auto prepared = prepare_transaction(world, tx, verifier);
                if (!prepared.committable) {
                    result.rejected += 1;
                    result.violations += prepared.verification.violations.size();
                    result.messages.push_back(IngestionMessage{
                        0,
                        id,
                        prepared.rejection_reason.empty() ? "transaction rejected" : prepared.rejection_reason});
                    continue;
                }
            }

            auto record = repository_.commit(options.branch, std::move(tx), verifier);
            if (!record.has_value()) {
                result.errors += 1;
                result.messages.push_back(IngestionMessage{0, id, "repository refused to create a commit record"});
                continue;
            }
            result.violations += record->violations.size();
            if (!record->accepted) {
                result.rejected += 1;
                result.messages.push_back(IngestionMessage{0, id, "transaction rejected"});
                continue;
            }

            index.mark_seen("graph-log", id, record->id);
            result.accepted += 1;
        } catch (const std::exception& error) {
            result.errors += 1;
            result.messages.push_back(IngestionMessage{line_number, {}, error.what()});
        }
    }

    return result;
}

}  // namespace pv
