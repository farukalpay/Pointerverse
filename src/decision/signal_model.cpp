// SPDX-License-Identifier: Apache-2.0
#include "pv/decision/signal_model.hpp"

#include <fmt/format.h>

#include <algorithm>

namespace pv {
namespace {

bool has_evidence(const Signal& signal) {
    return !signal.evidence_event_ids.empty();
}

std::string priority_for(double score, double threshold) {
    return score >= threshold * 2.0 ? "high" : "medium";
}

}  // namespace

SignalModel::SignalModel(SignalModelOptions options) : options_(options) {}

std::vector<Signal> SignalModel::signals(
    const std::vector<EntityProjectionEntry>& entities,
    const std::vector<RelationProjectionEntry>& relations) const {
    std::vector<Signal> out;

    for (const auto& entity : entities) {
        if (entity.appearances < options_.high_activity_entity_threshold
            || entity.evidence_event_ids.empty()) {
            continue;
        }
        out.push_back(Signal{
            "signal/entity/" + entity.entity,
            entity.entity,
            "high_activity_entity",
            static_cast<double>(entity.appearances),
            fmt::format(
                "{} appears in {} graph events",
                entity.entity,
                entity.appearances),
            entity.evidence_event_ids
        });
    }

    for (const auto& relation : relations) {
        if (relation.occurrences < options_.repeated_relation_threshold
            || relation.evidence_event_ids.empty()) {
            continue;
        }
        out.push_back(Signal{
            "signal/relation/" + relation.relation,
            relation.relation,
            "repeated_relation",
            static_cast<double>(relation.occurrences),
            fmt::format(
                "{} repeats {} times",
                relation.relation,
                relation.occurrences),
            relation.evidence_event_ids
        });
    }

    std::ranges::sort(out, [](const auto& left, const auto& right) {
        if (left.score != right.score) {
            return left.score > right.score;
        }
        return left.id < right.id;
    });
    return out;
}

std::vector<Recommendation> SignalModel::recommendations(const std::vector<Signal>& signals) const {
    std::vector<Recommendation> out;
    for (const auto& signal : signals) {
        if (!has_evidence(signal)) {
            continue;
        }
        const auto threshold = signal.kind == "repeated_relation"
            ? static_cast<double>(options_.repeated_relation_threshold)
            : static_cast<double>(options_.high_activity_entity_threshold);
        out.push_back(Recommendation{
            "recommendation/" + signal.id,
            priority_for(signal.score, threshold),
            signal.kind == "repeated_relation" ? "review repeated relation cluster" : "inspect high-activity entity",
            signal.explanation,
            {signal}
        });
    }
    return out;
}

DecisionReport SignalModel::report(
    std::string_view branch,
    const std::vector<EntityProjectionEntry>& entities,
    const std::vector<RelationProjectionEntry>& relations) const {
    DecisionReport report;
    report.branch = std::string{branch};
    report.signals = signals(entities, relations);
    report.recommendations = recommendations(report.signals);
    return report;
}

}  // namespace pv
