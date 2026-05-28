// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "pv/law/law.hpp"

namespace pv {

struct VerificationResult {
    bool accepted{true};
    std::vector<LawStatus> statuses;
    std::vector<LawViolation> violations;
};

enum class VerificationMode {
    Strict,
    Observe
};

class Verifier {
public:
    explicit Verifier(VerificationMode mode = VerificationMode::Strict);

    void add(std::shared_ptr<Law> law);
    void add_builtin(std::string_view name, double tolerance = 1e-9);
    void set_mode(VerificationMode mode) noexcept;

    [[nodiscard]] VerificationResult check(const LawCheckContext& ctx) const;
    [[nodiscard]] const std::vector<std::shared_ptr<Law>>& laws() const noexcept;
    [[nodiscard]] VerificationMode mode() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    VerificationMode mode_{VerificationMode::Strict};
    std::vector<std::shared_ptr<Law>> laws_;
};

}  // namespace pv
