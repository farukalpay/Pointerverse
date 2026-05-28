// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <string_view>

namespace pv {

class Repository;

class ExplanationEngine {
public:
    [[nodiscard]] std::string explain_object(
        const Repository& repository,
        std::string_view branch,
        std::string_view object_name) const;
    [[nodiscard]] std::string explain_commit(
        const Repository& repository,
        std::string_view branch,
        std::string_view commit_prefix) const;
    [[nodiscard]] std::string why_relation(
        const Repository& repository,
        std::string_view branch,
        std::string_view from,
        std::string_view relation,
        std::string_view to) const;
};

}  // namespace pv
