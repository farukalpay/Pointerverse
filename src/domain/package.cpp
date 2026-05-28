// SPDX-License-Identifier: Apache-2.0
#include "pv/domain/package.hpp"

#include <utility>

#include "pv/domain/agent_audit.hpp"

namespace pv {

DomainRegistry DomainRegistry::with_builtins() {
    DomainRegistry registry;
    registry.add(make_agent_audit_domain());
    return registry;
}

void DomainRegistry::add(DomainPackage package) {
    for (auto& existing : packages_) {
        if (existing.name == package.name) {
            existing = std::move(package);
            return;
        }
    }
    packages_.push_back(std::move(package));
}

std::optional<DomainPackage> DomainRegistry::find(std::string_view name) const {
    for (const auto& package : packages_) {
        if (package.name == name) {
            return package;
        }
    }
    return std::nullopt;
}

const std::vector<DomainPackage>& DomainRegistry::packages() const noexcept {
    return packages_;
}

}  // namespace pv
