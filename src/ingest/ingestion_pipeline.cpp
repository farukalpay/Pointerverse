// SPDX-License-Identifier: Apache-2.0
#include "pv/ingest/ingestion_pipeline.hpp"

#include <fmt/format.h>

#include <map>
#include <stdexcept>
#include <utility>

#include "pv/domain/agent_audit.hpp"
#include "pv/domain/domain.hpp"
#include "pv/ingest/evidence.hpp"
#include "pv/ingest/ingestion_index.hpp"
#include "pv/ingest/normalizer.hpp"
#include "pv/rule/rule_engine.hpp"
#include "pv/runtime/transaction.hpp"
#include "pv/storage/repository.hpp"

namespace pv {
namespace {

ObjectRef object_ref(
    World& world,
    Delta& delta,
    std::map<std::string, TempObjectId>& temps,
    std::uint32_t& next_temp,
    const std::string& name,
    const std::string& type) {
    if (world.has_object(name)) {
        return ObjectRef{world.object_by_name(name)};
    }
    if (const auto iter = temps.find(name); iter != temps.end()) {
        return ObjectRef{iter->second};
    }

    const auto temp = TempObjectId{next_temp++};
    temps.emplace(name, temp);
    delta.append_create(ObjectCreate{temp, name, world.type_id(type), ExistenceState::Alive, {}});
    return ObjectRef{temp};
}

Transaction transaction_for(World& world, const NormalizedAuditEvent& event) {
    Delta delta;
    std::map<std::string, TempObjectId> temps;
    std::uint32_t next_temp = 1;

    const auto from = object_ref(world, delta, temps, next_temp, event.from, event.from_type);
    const auto to = object_ref(world, delta, temps, next_temp, event.to, event.to_type);
    const auto evidence_name = "Evidence/" + event.source + "/" + event.evidence_id;
    const auto evidence = object_ref(world, delta, temps, next_temp, evidence_name, "Evidence");

    delta.append_link(PointerCreate{
        from,
        to,
        world.relation_type(event.relation),
        CausalRole::Structural,
        Weight{1.0},
        "agent_audit",
        {}
    });
    delta.append_link(PointerCreate{
        evidence,
        to,
        world.relation_type("backs"),
        CausalRole::Observational,
        Weight{1.0},
        "agent_audit",
        {}
    });
    delta.append_event(TraceEvent{
        {},
        "evidence.ingest",
        {
            {"source", event.source},
            {"event_id", event.evidence_id},
            {"action", event.action},
            {"actor", event.actor},
            {"from", event.from},
            {"from_type", event.from_type},
            {"relation", event.relation},
            {"to", event.to},
            {"to_type", event.to_type},
            {"timestamp_ms", std::to_string(event.timestamp_ms)}
        },
        {}
    });

    Transaction tx;
    tx.origin = TransactionOrigin::Ingestion;
    tx.label = fmt::format("ingest {} {} {}", event.source, event.evidence_id, event.action);
    tx.delta = std::move(delta);
    return tx;
}

Verifier verifier_for(const DomainPackage& package, VerificationMode mode) {
    RuleEngine rules;
    rules.add_all(package.rules);

    Verifier verifier{mode};
    for (const auto& rule : rules.rules()) {
        verifier.add(rules.make_law(rule.name));
    }
    return verifier;
}

void add_message(IngestionResult& result, const EvidenceEvent& event, std::string message) {
    result.messages.push_back(IngestionMessage{0, event.event_id, std::move(message)});
}

}  // namespace

IngestionPipeline::IngestionPipeline(Repository& repository) : repository_(repository) {}

IngestionResult IngestionPipeline::ingest(
    const std::vector<EvidenceEvent>& events,
    const EvidenceNormalizer& normalizer,
    IngestionIndex& index,
    const IngestionOptions& options) {
    if (options.domain != "agent_audit") {
        throw std::invalid_argument("M5 ingestion supports only --domain agent_audit");
    }
    if (options.branch.empty()) {
        throw std::invalid_argument("ingestion branch cannot be empty");
    }
    if (!repository_.has_branch(options.branch)) {
        (void)repository_.create_branch(options.branch, World{"audit"});
    }

    auto& world = repository_.mutable_world(options.branch);
    const auto package = make_agent_audit_domain();
    install_domain_schema(world, package);
    auto verifier = verifier_for(package, options.mode);

    IngestionResult result;
    result.events_read = events.size();

    for (const auto& event : events) {
        if (!valid_evidence_key(event.source) || !valid_evidence_key(event.event_id)) {
            result.errors += 1;
            add_message(result, event, "invalid source or event_id for evidence index");
            continue;
        }
        if (index.seen(event.source, event.event_id)) {
            result.skipped_duplicates += 1;
            continue;
        }

        try {
            const auto normalized = normalizer.normalize(event);
            auto tx = transaction_for(world, normalized);
            if (options.mode == VerificationMode::Strict) {
                const auto prepared = prepare_transaction(world, tx, verifier);
                if (!prepared.committable) {
                    result.rejected += 1;
                    result.violations += prepared.verification.violations.size();
                    add_message(result, event, prepared.rejection_reason.empty()
                        ? "transaction rejected"
                        : prepared.rejection_reason);
                    continue;
                }
            }

            auto record = repository_.commit(options.branch, std::move(tx), verifier);
            if (!record.has_value()) {
                result.errors += 1;
                add_message(result, event, "repository refused to create a commit record");
                continue;
            }
            result.violations += record->violations.size();
            if (!record->accepted) {
                result.rejected += 1;
                add_message(result, event, "transaction rejected");
                continue;
            }

            index.mark_seen(event.source, event.event_id, record->id);
            result.accepted += 1;
        } catch (const std::exception& error) {
            result.errors += 1;
            add_message(result, event, error.what());
        }
    }

    return result;
}

}  // namespace pv
