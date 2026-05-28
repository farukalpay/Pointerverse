// SPDX-License-Identifier: Apache-2.0
#include "pv/law/verifier.hpp"

#include <fmt/format.h>
#include <stdexcept>

#include "pv/law/invariant.hpp"

namespace pv {
namespace {

bool rejects_transition(Severity severity) noexcept {
    return severity == Severity::Error || severity == Severity::Fatal;
}

}  // namespace

std::string to_string(Severity severity) {
    switch (severity) {
    case Severity::Info:
        return "info";
    case Severity::Warning:
        return "warning";
    case Severity::Error:
        return "error";
    case Severity::Fatal:
        return "fatal";
    }
    return "error";
}

Verifier::Verifier(VerificationMode mode) : mode_(mode) {}

void Verifier::add(std::shared_ptr<Law> law) {
    if (!law) {
        throw std::invalid_argument("law cannot be null");
    }
    if (law->name().empty()) {
        throw std::invalid_argument("law name cannot be empty");
    }
    laws_.push_back(std::move(law));
}

void Verifier::add_builtin(std::string_view name, double tolerance) {
    add(make_builtin_law(name, tolerance));
}

void Verifier::set_mode(VerificationMode mode) noexcept {
    mode_ = mode;
}

VerificationResult Verifier::check(const LawCheckContext& ctx) const {
    VerificationResult result;
    for (const auto& law : laws_) {
        auto violations = law->check(ctx);
        if (violations.empty()) {
            result.statuses.push_back(LawStatus{
                std::string{law->name()},
                true,
                Severity::Info,
                0.0,
                "stable"
            });
            continue;
        }

        for (auto& violation : violations) {
            if (violation.law.empty()) {
                violation.law = std::string{law->name()};
            }
            if (mode_ == VerificationMode::Strict && rejects_transition(violation.severity)) {
                result.accepted = false;
            }
            result.statuses.push_back(LawStatus{
                violation.law,
                false,
                violation.severity,
                violation.magnitude,
                violation.explanation
            });
            result.violations.push_back(std::move(violation));
        }
    }
    return result;
}

const std::vector<std::shared_ptr<Law>>& Verifier::laws() const noexcept {
    return laws_;
}

VerificationMode Verifier::mode() const noexcept {
    return mode_;
}

std::size_t Verifier::size() const noexcept {
    return laws_.size();
}

}  // namespace pv
