// SPDX-License-Identifier: Apache-2.0
#include "pv/kernel/fact_store.hpp"

#include <algorithm>

namespace pv {

FactStore FactStore::from_snapshot(const WorldSnapshot& snapshot) {
    FactStore store;
    store.facts_ = derive_facts(snapshot);
    return store;
}

std::span<const Fact> FactStore::facts() const noexcept {
    return facts_;
}

const Fact* FactStore::find(FactId id) const noexcept {
    const auto iter = std::ranges::find_if(facts_, [id](const Fact& fact) {
        return fact.id == id;
    });
    return iter == facts_.end() ? nullptr : &*iter;
}

}  // namespace pv
