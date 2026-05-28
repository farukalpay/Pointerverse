// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "pv/hash/canonical.hpp"

namespace pv {

enum class RegionKind : std::uint8_t {
    Manifest,
    BranchRef,
    CommitObject,
    SnapshotObject,
    ProgramObject,
    DeltaObject,
    TraceObject,
    LawObject,
    ProofObject,
    VmOpcodeTable,
    CompilerSymbolTable
};

struct IntegrityRegion {
    RegionKind kind{RegionKind::Manifest};
    std::string label;
    Hash256 expected;
    Hash256 observed;
    bool required{true};
};

struct RegionCheckReport {
    std::size_t regions_checked{0};
    std::size_t mismatches{0};
    std::size_t warnings{0};
    Hash256 root;
    std::vector<std::string> errors;
    std::vector<std::string> warning_messages;

    [[nodiscard]] bool clean() const noexcept { return errors.empty(); }
};

class RegionTable {
public:
    void add(IntegrityRegion region);
    [[nodiscard]] RegionCheckReport verify() const;
    [[nodiscard]] const std::vector<IntegrityRegion>& regions() const noexcept;

private:
    std::vector<IntegrityRegion> regions_;
};

[[nodiscard]] std::string to_string(RegionKind kind);

}  // namespace pv
