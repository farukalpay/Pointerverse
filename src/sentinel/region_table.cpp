// SPDX-License-Identifier: Apache-2.0
#include "pv/sentinel/region_table.hpp"

#include <algorithm>
#include <utility>

#include "pv/hash/hasher.hpp"
#include "pv/kernel/canonical_codec.hpp"

namespace pv {

std::string to_string(RegionKind kind) {
    switch (kind) {
    case RegionKind::Manifest:
        return "manifest";
    case RegionKind::BranchRef:
        return "branch-ref";
    case RegionKind::CommitObject:
        return "commit-object";
    case RegionKind::SnapshotObject:
        return "snapshot-object";
    case RegionKind::ProgramObject:
        return "program-object";
    case RegionKind::DeltaObject:
        return "delta-object";
    case RegionKind::TraceObject:
        return "trace-object";
    case RegionKind::LawObject:
        return "law-object";
    case RegionKind::ProofObject:
        return "proof-object";
    case RegionKind::VmOpcodeTable:
        return "vm-opcode-table";
    case RegionKind::CompilerSymbolTable:
        return "compiler-symbol-table";
    }
    return "unknown";
}

void RegionTable::add(IntegrityRegion region) {
    regions_.push_back(std::move(region));
}

RegionCheckReport RegionTable::verify() const {
    RegionCheckReport report;
    report.regions_checked = regions_.size();

    auto regions = regions_;
    std::ranges::sort(regions, [](const IntegrityRegion& left, const IntegrityRegion& right) {
        if (left.kind != right.kind) {
            return static_cast<std::uint8_t>(left.kind) < static_cast<std::uint8_t>(right.kind);
        }
        return left.label < right.label;
    });

    CanonicalWriter writer;
    writer.string("PointerverseRegionTable:v1");
    writer.u64(regions.size());
    for (const auto& region : regions) {
        writer.u8(static_cast<std::uint8_t>(region.kind));
        writer.string(region.label);
        writer.hash(region.expected);
        writer.hash(region.observed);
        writer.u8(region.required ? 1 : 0);
        if (region.expected != region.observed) {
            report.mismatches += 1;
            const auto message = to_string(region.kind) + " mismatch: " + region.label;
            if (region.required) {
                report.errors.push_back(message);
            } else {
                report.warnings += 1;
                report.warning_messages.push_back(message);
            }
        }
    }
    report.root = sha256(writer.bytes());
    return report;
}

const std::vector<IntegrityRegion>& RegionTable::regions() const noexcept {
    return regions_;
}

}  // namespace pv
