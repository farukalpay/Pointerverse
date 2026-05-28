// SPDX-License-Identifier: Apache-2.0
#include "pv/runtime/ids.hpp"

#include <fmt/format.h>

namespace pv {

std::string to_string(BranchId id) {
    return id.valid() ? fmt::format("B{}", id.value) : "B<invalid>";
}

std::string to_string(TransactionId id) {
    return id.valid() ? fmt::format("T{}", id.value) : "T<invalid>";
}

std::string to_string(CommitId id) {
    return id.valid() ? to_hex(id.value) : "C<invalid>";
}

std::string to_string(SnapshotId id) {
    return id.valid() ? fmt::format("S{}", id.value) : "S<invalid>";
}

}  // namespace pv
