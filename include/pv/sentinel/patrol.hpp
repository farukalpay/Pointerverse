// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "pv/hash/canonical.hpp"
#include "pv/runtime/ids.hpp"
#include "pv/sentinel/report.hpp"

namespace pv {

class Repository;
struct CommitRecord;

struct ProgramReplayCheck {
    CommitId commit;
    Hash256 program_hash;
    Hash256 expected_delta_hash;
    Hash256 observed_delta_hash;
    bool matched{false};
    std::vector<std::string> diagnostics;
};

struct CommitProofCheck {
    CommitId commit;
    bool matched{false};
    std::vector<std::string> diagnostics;
};

class StorePatrolWorker {
public:
    [[nodiscard]] SentinelReport run(const std::filesystem::path& root) const;
};

class ProofPatrolWorker {
public:
    [[nodiscard]] SentinelReport run(const Repository& repo) const;
};

class VmReplayWorker {
public:
    [[nodiscard]] SentinelReport run(const Repository& repo, std::size_t max_programs = 0) const;
};

[[nodiscard]] CommitProofCheck check_commit_proof(const Repository& repo, const CommitRecord& record);
[[nodiscard]] ProgramReplayCheck check_program_replay(const Repository& repo, const CommitRecord& record);
[[nodiscard]] SentinelReport patrol_repository(const Repository& repo);

}  // namespace pv
