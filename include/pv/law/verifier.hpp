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

class Verifier {
public:
    void add(std::shared_ptr<Law> law);
    void add_builtin(std::string_view name, double tolerance = 1e-9);

    [[nodiscard]] VerificationResult check(const LawCheckContext& ctx) const;
    [[nodiscard]] const std::vector<std::shared_ptr<Law>>& laws() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    std::vector<std::shared_ptr<Law>> laws_;
};

}  // namespace pv
