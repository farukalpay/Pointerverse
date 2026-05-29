// SPDX-License-Identifier: Apache-2.0
#include "pv/decision/signal_model.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>

namespace pv {
namespace {

bool has_evidence(const Signal& signal) {
    return !signal.evidence_event_ids.empty();
}

double quantile(std::vector<double> values, double percentile) {
    if (values.empty()) {
        return 0.0;
    }
    std::ranges::sort(values);
    if (values.size() == 1) {
        return values.front();
    }
    const auto clamped = std::clamp(percentile, 0.0, 1.0);
    const auto index = static_cast<std::size_t>(
        std::ceil(clamped * static_cast<double>(values.size() - 1U)));
    return values[std::min(index, values.size() - 1U)];
}

std::string priority_for(const Signal& signal) {
    if (signal.score >= signal.critical_threshold) {
        return "critical";
    }
    if (signal.score >= signal.high_threshold) {
        return "high";
    }
    return "medium";
}

}  // namespace

SignalModel::SignalModel(SignalModelOptions options) : options_(options) {}

std::vector<Signal> SignalModel::signals(
    const std::vector<EntityProjectionEntry>& entities,
    const std::vector<RelationProjectionEntry>& relations) const {
    std::vector<Signal> out;
    std::vector<double> entity_scores;
    std::vector<double> relation_scores;
    entity_scores.reserve(entities.size());
    relation_scores.reserve(relations.size());
    for (const auto& entity : entities) {
        entity_scores.push_back(static_cast<double>(entity.appearances));
    }
    for (const auto& relation : relations) {
        relation_scores.push_back(static_cast<double>(relation.occurrences));
    }
    const auto entity_medium = quantile(entity_scores, options_.thresholds.medium_quantile);
    const auto entity_high = quantile(entity_scores, options_.thresholds.high_quantile);
    const auto entity_critical = quantile(entity_scores, options_.thresholds.critical_quantile);
    const auto relation_medium = quantile(relation_scores, options_.thresholds.medium_quantile);
    const auto relation_high = quantile(relation_scores, options_.thresholds.high_quantile);
    const auto relation_critical = quantile(relation_scores, options_.thresholds.critical_quantile);

    for (const auto& entity : entities) {
        if (static_cast<double>(entity.appearances) < entity_medium
            || entity.evidence_event_ids.empty()) {
            continue;
        }
        out.push_back(Signal{
            "signal/entity/" + entity.entity,
            entity.entity,
            "high_activity_entity",
            static_cast<double>(entity.appearances),
            entity_medium,
            entity_high,
            entity_critical,
            fmt::format(
                "{} appears in {} graph events",
                entity.entity,
                entity.appearances),
            entity.evidence_event_ids
        });
    }

    for (const auto& relation : relations) {
        if (static_cast<double>(relation.occurrences) < relation_medium
            || relation.evidence_event_ids.empty()) {
            continue;
        }
        out.push_back(Signal{
            "signal/relation/" + relation.relation,
            relation.relation,
            "repeated_relation",
            static_cast<double>(relation.occurrences),
            relation_medium,
            relation_high,
            relation_critical,
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
        out.push_back(Recommendation{
            "recommendation/" + signal.id,
            priority_for(signal),
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
