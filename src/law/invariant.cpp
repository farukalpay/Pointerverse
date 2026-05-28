// SPDX-License-Identifier: Apache-2.0
#include "pv/law/invariant.hpp"

#include <cmath>
#include <fmt/format.h>
#include <memory>
#include <stdexcept>

namespace pv {
namespace {

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
                violations.push_back({
                    name_,
                    Severity::Error,
                    1.0,
                    fmt::format("pointer {} references an object outside the snapshot", to_string(pointer.id))
                });
            }
        }
        return violations;
    }

    [[nodiscard]] std::vector<LawViolation> bounded_weight(const LawCheckContext& ctx) const {
        std::vector<LawViolation> violations;
        for (const auto& pointer : ctx.after.pointers) {
            if (!std::isfinite(pointer.weight.value) || pointer.weight.value < 0.0 || pointer.weight.value > 1.0 + tolerance_) {
                violations.push_back({
                    name_,
                    Severity::Error,
                    std::abs(pointer.weight.value),
                    fmt::format("pointer {} has out-of-bound weight {:.12g}", to_string(pointer.id), pointer.weight.value)
                });
            }
        }
        return violations;
    }

    [[nodiscard]] std::vector<LawViolation> preserve_relation_type(const LawCheckContext& ctx) const {
        std::vector<LawViolation> violations;
        for (const auto& pointer : ctx.after.pointers) {
            if (!pointer.relation.valid() || !ctx.after.relation_names.contains(pointer.relation.id)) {
                violations.push_back({
                    name_,
                    Severity::Error,
                    1.0,
                    fmt::format("pointer {} has no registered relation type", to_string(pointer.id))
                });
            }
        }
        return violations;
    }

    [[nodiscard]] std::vector<LawViolation> no_invalid_epoch_reference(const LawCheckContext& ctx) const {
        std::vector<LawViolation> violations;
        for (const auto& pointer : ctx.after.pointers) {
            if (pointer.born_at > ctx.after.epoch) {
                violations.push_back({
                    name_,
                    Severity::Error,
                    static_cast<double>(pointer.born_at.value - ctx.after.epoch.value),
                    fmt::format("pointer {} is born after the snapshot epoch", to_string(pointer.id))
                });
            }
            if (pointer.expires_at.has_value() && *pointer.expires_at < pointer.born_at) {
                violations.push_back({
                    name_,
                    Severity::Error,
                    1.0,
                    fmt::format("pointer {} expires before it is born", to_string(pointer.id))
                });
            }
        }
        return violations;
    }

    [[nodiscard]] std::vector<LawViolation> preserve_existing_identity(const LawCheckContext& ctx) const {
        std::vector<LawViolation> violations;
        for (const auto& object : ctx.before.objects) {
            const auto* after = ctx.after.object(object.id);
            if (after == nullptr) {
                violations.push_back({
                    name_,
                    Severity::Fatal,
                    1.0,
                    fmt::format("object {} disappeared instead of changing existence state", to_string(object.id))
                });
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
