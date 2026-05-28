// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <span>
#include <vector>

#include "pv/core/fact.hpp"
#include "pv/core/snapshot.hpp"
#include "pv/hash/canonical.hpp"

namespace pv {

struct WorldRoot {
    Hash256 object_root;
    Hash256 pointer_root;
    Hash256 fact_root;
    Hash256 type_root;
    Hash256 relation_root;
    Hash256 root;
};

[[nodiscard]] WorldRoot compute_world_root(const WorldSnapshot& snapshot);
[[nodiscard]] Hash256 compute_fact_id_root(std::span<const FactId> facts);
[[nodiscard]] Hash256 compute_hash_list_root(std::span<const Hash256> hashes, std::string_view label);

}  // namespace pv
