// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "pv/domain/domain.hpp"

namespace pv {

class DomainRegistry {
public:
    static DomainRegistry with_builtins();

    void add(DomainPackage package);

    [[nodiscard]] std::optional<DomainPackage> find(std::string_view name) const;
    [[nodiscard]] const std::vector<DomainPackage>& packages() const noexcept;

private:
    std::vector<DomainPackage> packages_;
};

}  // namespace pv
