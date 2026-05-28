// SPDX-License-Identifier: Apache-2.0
#include "pv/domain/domain.hpp"

namespace pv {

void install_domain_schema(World& world, const DomainPackage& package) {
    for (const auto& type : package.schema.object_types) {
        (void)world.type_id(type);
    }
    for (const auto& relation : package.schema.relations) {
        (void)world.relation_type(relation);
    }
}

}  // namespace pv
