// SPDX-License-Identifier: Apache-2.0
#include "pv/runtime/transaction.hpp"

#include <fmt/format.h>

#include <stdexcept>

namespace pv {
namespace {

std::string overlay_reason(OverlayError error) {
    return fmt::format("overlay rejected transaction: {}", to_string(error));
}

void append_law_trace(World& world, std::vector<TraceEvent>& events, const VerificationResult& verification) {
    for (const auto& status : verification.statuses) {
        events.push_back(TraceEvent{
            world.epoch(),
            "law.check",
            {
                {"world", world.name()},
                {"law", status.law},
                {"status", status.passed ? "ok" : to_string(status.severity)},
                {"detail", status.explanation}
            },
            {{"magnitude", status.magnitude}}
        });
    }
}

}  // namespace

PreparedTransaction prepare_transaction(const World& world, const Transaction& tx, const Verifier& verifier) {
    PreparedTransaction prepared;
    prepared.tx = tx;
    prepared.before = world.snapshot();
    prepared.predicted_after = SnapshotOverlay{prepared.before}.apply(tx.delta);

    if (tx.delta.empty() && !tx.allow_empty) {
        prepared.rejection_reason = "empty transaction rejected";
        return prepared;
    }

    if (tx.expected_base_epoch.has_value() && *tx.expected_base_epoch != prepared.before.epoch) {
        prepared.rejection_reason = fmt::format(
            "transaction expected base epoch {}, got {}",
            tx.expected_base_epoch->value,
            prepared.before.epoch.value);
        return prepared;
    }

    if (tx.input_snapshot_hash.has_value() && *tx.input_snapshot_hash != prepared.before.canonical_hash()) {
        prepared.rejection_reason = "transaction input snapshot hash does not match current world";
        return prepared;
    }

    if (!prepared.predicted_after.has_value()) {
        prepared.rejection_reason = overlay_reason(prepared.predicted_after.error());
        return prepared;
    }

    prepared.verification = verifier.check(LawCheckContext{prepared.before, tx.delta, *prepared.predicted_after});
    if (!prepared.verification.accepted) {
        prepared.rejection_reason = "transition rejected by active laws";
        return prepared;
    }

    try {
        prepared.predicted_events = world.preview_delta_unchecked(tx.delta);
    } catch (const std::exception& error) {
        prepared.rejection_reason = error.what();
        return prepared;
    }

    prepared.committable = true;
    return prepared;
}

CommitResult commit_prepared(World& world, const PreparedTransaction& prepared) {
    CommitResult result;
    result.before_epoch = world.epoch();
    result.law_statuses = prepared.verification.statuses;
    result.violations = prepared.verification.violations;

    const auto current_hash = world.canonical_hash();
    const auto prepared_hash = prepared.before.canonical_hash();
    if (world.epoch() != prepared.before.epoch || current_hash != prepared_hash) {
        result.events = world.append_rejection_trace(
            prepared.tx.delta,
            "prepared transaction does not match current world",
            {});
        result.after_epoch = world.epoch();
        result.accepted = false;
        result.world_hash = world.hash();
        return result;
    }

    if (!prepared.committable) {
        result.events = world.append_rejection_trace(
            prepared.tx.delta,
            prepared.rejection_reason.empty() ? "transaction rejected" : prepared.rejection_reason,
            prepared.verification.violations);
        result.after_epoch = world.epoch();
        result.accepted = false;
        result.world_hash = world.hash();
        return result;
    }

    auto events = world.apply_delta_unchecked(prepared.tx.delta);
    append_law_trace(world, events, prepared.verification);
    world.trace_.append(events);

    result.accepted = true;
    result.after_epoch = world.epoch();
    result.events = std::move(events);
    result.world_hash = world.hash();
    return result;
}

std::string to_string(TransactionOrigin origin) {
    switch (origin) {
    case TransactionOrigin::Manual:
        return "Manual";
    case TransactionOrigin::Script:
        return "Script";
    case TransactionOrigin::Morphism:
        return "Morphism";
    case TransactionOrigin::Evolution:
        return "Evolution";
    case TransactionOrigin::Replay:
        return "Replay";
    case TransactionOrigin::ForkMerge:
        return "ForkMerge";
    case TransactionOrigin::Internal:
        return "Internal";
    case TransactionOrigin::Ingestion:
        return "Ingestion";
    }
    return "Manual";
}

}  // namespace pv
