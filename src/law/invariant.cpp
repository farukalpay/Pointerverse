// SPDX-License-Identifier: Apache-2.0
#include "pv/law/invariant.hpp"

#include <cmath>
#include <fmt/format.h>
#include <memory>
#include <stdexcept>

#include "pv/kernel/fact_store.hpp"

namespace pv {
namespace {

std::vector<FactId> pointer_evidence(const LawCheckContext& ctx, PointerId pointer) {
    std::vector<FactId> out;
    if (ctx.after_facts == nullptr) {
        return out;
    }
    for (const auto& fact : ctx.after_facts->facts()) {
        if (const auto* payload = std::get_if<PointerFactPayload>(&fact.payload);
            payload != nullptr && payload->pointer == pointer) {
            out.push_back(fact.id);
        }
    }
    return out;
}

std::vector<FactId> object_evidence(const LawCheckContext& ctx, ObjectId object) {
    std::vector<FactId> out;
    if (ctx.before_facts == nullptr) {
        return out;
    }
    for (const auto& fact : ctx.before_facts->facts()) {
        if (const auto* payload = std::get_if<ObjectFactPayload>(&fact.payload);
            payload != nullptr && payload->object == object) {
            out.push_back(fact.id);
        }
    }
    return out;
}

LawViolation pointer_violation(
    const LawCheckContext& ctx,
    std::string law,
    Severity severity,
    double magnitude,
    std::string explanation,
    const PointerSnapshot& pointer) {
    LawViolation violation;
    violation.law = std::move(law);
    violation.severity = severity;
    violation.magnitude = magnitude;
    violation.explanation = std::move(explanation);
    violation.evidence = pointer_evidence(ctx, pointer.id);
    violation.objects = {pointer.from, pointer.to};
    violation.pointers = {pointer.id};
    return violation;
}

class BuiltinLaw final : public Law {
public:
    BuiltinLaw(std::string name, double tolerance) : name_(std::move(name)), tolerance_(tolerance) {}

    [[nodiscard]] std::string_view name() const override {
        return name_;
    }

    [[nodiscard]] std::vector<LawViolation> check(const LawCheckContext& ctx) const override {
        if (name_ == "reject_dangling_pointer") {
            return reject_dangling_pointer(ctx);
        }
        if (name_ == "bounded_weight") {
            return bounded_weight(ctx);
        }
        if (name_ == "preserve_relation_type") {
            return preserve_relation_type(ctx);
        }
        if (name_ == "no_invalid_epoch_reference") {
            return no_invalid_epoch_reference(ctx);
        }
        if (name_ == "preserve_existing_identity") {
            return preserve_existing_identity(ctx);
        }
        throw std::logic_error(fmt::format("unimplemented builtin law '{}'", name_));
    }

private:
    [[nodiscard]] std::vector<LawViolation> reject_dangling_pointer(const LawCheckContext& ctx) const {
        std::vector<LawViolation> violations;
        for (const auto& pointer : ctx.after.pointers) {
            if (!ctx.after.contains(pointer.from) || !ctx.after.contains(pointer.to)) {
                double missing_endpoints = 0.0;
                if (!ctx.after.contains(pointer.from)) {
                    missing_endpoints += 1.0;
                }
                if (!ctx.after.contains(pointer.to)) {
                    missing_endpoints += 1.0;
                }
                violations.push_back(pointer_violation(
                    ctx,
                    name_,
                    Severity::Error,
                    missing_endpoints,
                    fmt::format("pointer {} references an object outside the snapshot", to_string(pointer.id)),
                    pointer));
            }
        }
        return violations;
    }

    [[nodiscard]] std::vector<LawViolation> bounded_weight(const LawCheckContext& ctx) const {
        std::vector<LawViolation> violations;
        for (const auto& pointer : ctx.after.pointers) {
            if (!std::isfinite(pointer.weight.value) || pointer.weight.value < 0.0 || pointer.weight.value > 1.0 + tolerance_) {
                double distance = 1.0;
                if (std::isfinite(pointer.weight.value)) {
                    if (pointer.weight.value < 0.0) {
                        distance = std::abs(pointer.weight.value);
                    } else if (pointer.weight.value > 1.0 + tolerance_) {
                        distance = pointer.weight.value - 1.0;
                    }
                }
                violations.push_back(pointer_violation(
                    ctx,
                    name_,
                    Severity::Error,
                    distance,
                    fmt::format("pointer {} has out-of-bound weight {:.12g}", to_string(pointer.id), pointer.weight.value),
                    pointer));
            }
        }
        return violations;
    }

    [[nodiscard]] std::vector<LawViolation> preserve_relation_type(const LawCheckContext& ctx) const {
        std::vector<LawViolation> violations;
        for (const auto& pointer : ctx.after.pointers) {
            if (!pointer.relation.valid() || !ctx.after.relation_names.contains(pointer.relation.id)) {
                violations.push_back(pointer_violation(
                    ctx,
                    name_,
                    Severity::Error,
                    1.0,
                    fmt::format("pointer {} has no registered relation type", to_string(pointer.id)),
                    pointer));
            }
        }
        return violations;
    }

    [[nodiscard]] std::vector<LawViolation> no_invalid_epoch_reference(const LawCheckContext& ctx) const {
        std::vector<LawViolation> violations;
        for (const auto& pointer : ctx.after.pointers) {
            if (pointer.born_at > ctx.after.epoch) {
                violations.push_back(pointer_violation(
                    ctx,
                    name_,
                    Severity::Error,
                    static_cast<double>(pointer.born_at.value - ctx.after.epoch.value),
                    fmt::format("pointer {} is born after the snapshot epoch", to_string(pointer.id)),
                    pointer));
            }
            if (pointer.expires_at.has_value() && *pointer.expires_at < pointer.born_at) {
                violations.push_back(pointer_violation(
                    ctx,
                    name_,
                    Severity::Error,
                    static_cast<double>(pointer.born_at.value - pointer.expires_at->value),
                    fmt::format("pointer {} expires before it is born", to_string(pointer.id)),
                    pointer));
            }
        }
        return violations;
    }

    [[nodiscard]] std::vector<LawViolation> preserve_existing_identity(const LawCheckContext& ctx) const {
        std::vector<LawViolation> violations;
        for (const auto& object : ctx.before.objects) {
            const auto* after = ctx.after.object(object.id);
            if (after == nullptr) {
                LawViolation violation;
                violation.law = name_;
                violation.severity = Severity::Fatal;
                violation.magnitude = 1.0;
                violation.explanation = fmt::format("object {} disappeared instead of changing existence state", to_string(object.id));
                violation.evidence = object_evidence(ctx, object.id);
                violation.objects = {object.id};
                violations.push_back(std::move(violation));
            }
        }
        return violations;
    }

    std::string name_;
    double tolerance_{1e-9};
};

}  // namespace

std::shared_ptr<Law> make_builtin_law(std::string_view name, double tolerance) {
    const std::string key{name};
    if (key == "reject_dangling_pointer"
        || key == "bounded_weight"
        || key == "preserve_relation_type"
        || key == "no_invalid_epoch_reference"
        || key == "preserve_existing_identity") {
        return std::make_shared<BuiltinLaw>(key, tolerance);
    }
    throw std::invalid_argument(fmt::format("unknown builtin law '{}'", key));
}

}  // namespace pv
