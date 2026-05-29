// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "pv/domain/domain.hpp"

namespace pv {

// Parse a reusable domain package file. A package may declare a schema and a set
// of rules so a whole domain can be shared as one file:
//
//   domain succession
//   type Crown
//   type Claimant
//   relation claims
//   relation bloodline
//
//   rule no_claim_without_bloodline
//   when link Claimant -> Crown : claims
//   require before link Claimant -> Crown : bloodline
//   deny reason "{from} claims {to} without bloodline continuity"
//
// Files that contain only rule blocks parse to a package with an empty schema,
// so existing rule-only domain files keep working.
[[nodiscard]] DomainPackage parse_domain_package(std::string_view text);

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
