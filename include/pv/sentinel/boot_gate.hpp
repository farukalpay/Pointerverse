// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "pv/hash/canonical.hpp"

namespace pv {

enum class BootStage : std::uint8_t {
    Manifest,
    ObjectStore,
    BranchRefs,
    CommitGraph,
    SnapshotLoad,
    VmReplay,
    ProofChain,
    Ready
};

struct BootMeasurement {
    Hash256 manifest_root;
    Hash256 ref_root;
    Hash256 branch_root;
    Hash256 commit_graph_root;
    Hash256 latest_world_root;
    Hash256 sentinel_root;
    Hash256 root;
};

struct BootGateResult {
    bool ok{false};
    BootStage failed_at{BootStage::Manifest};
    std::vector<std::string> diagnostics;
    BootMeasurement measurement;

    [[nodiscard]] Hash256 boot_measurement() const noexcept { return measurement.root; }
};

[[nodiscard]] std::string to_string(BootStage stage);
[[nodiscard]] BootGateResult run_boot_gate(const std::filesystem::path& root);
[[nodiscard]] std::string render_boot_gate_result(const BootGateResult& result);

}  // namespace pv
