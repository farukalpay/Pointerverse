// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace pv {

enum class FaultObjectKind {
    Snapshot,
    Commit,
    Program,
    Delta,
    Trace,
    Law,
    Proof
};

struct FaultInjectionResult {
    bool mutated{false};
    std::string target;
    std::string message;
};

struct FaultInjectionOptions {
    std::filesystem::path root;
    std::string branch{"main"};
    std::string commit{"HEAD"};
    FaultObjectKind kind{FaultObjectKind::Snapshot};
    bool confirm_mutates_store{false};
};

[[nodiscard]] FaultObjectKind parse_fault_object_kind(std::string_view text);
[[nodiscard]] FaultInjectionResult corrupt_object_fault(const FaultInjectionOptions& options);
[[nodiscard]] FaultInjectionResult flip_proof_fault(const FaultInjectionOptions& options);
[[nodiscard]] FaultInjectionResult remove_program_fault(const FaultInjectionOptions& options);
[[nodiscard]] FaultInjectionResult rewrite_ref_fault(const FaultInjectionOptions& options);

}  // namespace pv
