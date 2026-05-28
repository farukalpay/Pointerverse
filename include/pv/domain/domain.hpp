// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#include "pv/core/world.hpp"
#include "pv/domain/schema.hpp"
#include "pv/rule/rule.hpp"

namespace pv {

struct DomainPackage {
    std::string name;
    DomainSchema schema;
    std::vector<Rule> rules;
};

void install_domain_schema(World& world, const DomainPackage& package);

}  // namespace pv
