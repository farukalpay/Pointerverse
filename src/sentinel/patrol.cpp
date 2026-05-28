// SPDX-License-Identifier: Apache-2.0
#include "pv/sentinel/patrol.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "pv/hash/hasher.hpp"
#include "pv/kernel/merkle.hpp"
#include "pv/kernel/program.hpp"
#include "pv/kernel/proof.hpp"
#include "pv/kernel/vm.hpp"
#include "pv/sentinel/region_table.hpp"
#include "pv/kernel/canonical_codec.hpp"
#include "pv/storage/object_codec.hpp"
#include "pv/storage/repository.hpp"

namespace pv {
namespace {

std::vector<std::byte> read_raw_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open object '" + path.string() + "'");
    }
    std::vector<std::byte> out;
    for (std::istreambuf_iterator<char> iter{input}, end; iter != end; ++iter) {
        out.push_back(static_cast<std::byte>(static_cast<unsigned char>(*iter)));
    }
    return out;
}

Hash256 hash_file_if_exists(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return Hash256{};
    }
    return sha256(read_raw_bytes(path));
}

Hash256 vm_opcode_table_root() {
    CanonicalWriter writer;
    writer.string("PointerverseVmOpcodeTable:v1");
    writer.u64(10);
    writer.string("TypeIntern");
    writer.string("RelationIntern");
    writer.string("ObjectCreate");
    writer.string("ObjectUpdate");
    writer.string("PointerCreate");
    writer.string("PointerRemove");
    writer.string("AssertObject");
    writer.string("AssertPointer");
    writer.string("AssertFact");
    writer.string("EmitEvent");
    return sha256(writer.bytes());
}

Hash256 law_output_root(const std::vector<LawStatus>& statuses, const std::vector<LawViolation>& violations) {
    CanonicalWriter writer;
    writer.string("LawOutputRoot:v1");
    writer.u64(2);
    writer.hash(canonical_hash(statuses));
    writer.hash(canonical_hash(violations));
    return sha256(writer.bytes());
}

Hash256 legacy_proof_hash(const CommitProof& proof) {
    CanonicalWriter writer;
    writer.string("CommitProof:v1");
    writer.hash(proof.before_root);
    writer.hash(proof.operation_root);
    writer.hash(proof.read_set_root);
    writer.hash(proof.write_set_root);
    writer.hash(proof.law_input_root);
    writer.hash(proof.law_output_root);
    writer.hash(proof.after_root);
    return sha256(writer.bytes());
}

Hash256 report_measurement(const SentinelReport& report, std::string_view label) {
    CanonicalWriter writer;
    writer.string("SentinelReportMeasurement:v1");
    writer.string(label);
    writer.u64(report.regions_checked);
    writer.u64(report.commits_checked);
    writer.u64(report.snapshots_checked);
    writer.u64(report.objects_checked);
    writer.u64(report.branch_refs_checked);
    writer.u64(report.program_replays);
    writer.u64(report.proof_mismatches);
    writer.u64(report.store_corruptions);
    writer.u64(report.issues.size());
    for (const auto& issue : report.issues) {
        writer.string(issue.worker);
        writer.string(issue.message);
        writer.u8(issue.error ? 1 : 0);
    }
    return sha256(writer.bytes());
}

void append_check_diagnostics(
    SentinelReport& report,
    std::string_view worker,
    const std::vector<std::string>& diagnostics,
    CommitId commit) {
    for (const auto& diagnostic : diagnostics) {
        add_sentinel_error(report, std::string{worker}, diagnostic + ": " + to_hex(commit.value));
    }
}

}  // namespace

SentinelReport StorePatrolWorker::run(const std::filesystem::path& root) const {
    SentinelReport report;
    const auto object_root = root / "objects";
    if (!std::filesystem::exists(object_root)) {
        report.measurement = report_measurement(report, "store");
        return report;
    }

    CanonicalWriter writer;
    writer.string("StorePatrolWorker:v1");
    std::vector<std::pair<std::string, Hash256>> entries;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(object_root)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() == ".tmp") {
            continue;
        }
        report.objects_checked += 1;
        const auto filename = entry.path().filename().string();
        const auto parsed = parse_hash256(filename);
        if (!parsed.has_value()) {
            report.store_corruptions += 1;
            add_sentinel_error(report, "StorePatrolWorker", "object has invalid hash filename: " + entry.path().string());
            continue;
        }

        try {
            const auto bytes = read_raw_bytes(entry.path());
            const auto actual = sha256(bytes);
            entries.push_back({filename, actual});
            if (actual != *parsed) {
                report.store_corruptions += 1;
                add_sentinel_error(report, "StorePatrolWorker", "object blob hash does not match filename: " + filename);
            }
        } catch (const std::exception& error) {
            report.store_corruptions += 1;
            add_sentinel_error(report, "StorePatrolWorker", error.what());
        }
    }
    std::ranges::sort(entries, [](const auto& left, const auto& right) {
        return left.first < right.first;
    });
    writer.u64(entries.size());
    for (const auto& [filename, actual] : entries) {
        writer.string(filename);
        writer.hash(actual);
    }
    report.measurement = sha256(writer.bytes());
    return report;
}

CommitProofCheck check_commit_proof(const Repository& repo, const CommitRecord& record) {
    CommitProofCheck check;
    check.commit = record.id;
    check.matched = true;
    if (!record.proof.has_value()) {
        return check;
    }

    try {
        const auto stored = repo.objects().get_canonical<StoredCommit>(record.id.value);
        const auto before = repo.objects().get_canonical<WorldSnapshot>(stored.before_snapshot_object);
        const auto after = repo.objects().get_canonical<WorldSnapshot>(stored.after_snapshot_object);
        const auto statuses = repo.objects().get_canonical<std::vector<LawStatus>>(stored.law_status_object);
        const auto violations = repo.objects().get_canonical<std::vector<LawViolation>>(stored.violation_object);

        const auto proof_hash = hash_commit_proof(*record.proof);
        const auto accepts_legacy_hash = empty(record.program_hash)
            && legacy_proof_hash(*record.proof) == record.proof_hash;
        if (proof_hash != record.proof_hash && !accepts_legacy_hash) {
            check.matched = false;
            check.diagnostics.push_back("commit proof hash mismatch");
        }

        const auto before_root = compute_world_root(before);
        const auto after_root = compute_world_root(after);
        if (record.proof->before_root != before_root.root) {
            check.matched = false;
            check.diagnostics.push_back("commit proof before root mismatch");
        }
        if (record.proof->program_root != record.program_hash) {
            check.matched = false;
            check.diagnostics.push_back("commit proof program root mismatch");
        }
        if (record.proof->after_root != after_root.root) {
            check.matched = false;
            check.diagnostics.push_back("commit proof after root mismatch");
        }
        if (record.proof->operation_root != record.delta_hash) {
            check.matched = false;
            check.diagnostics.push_back("commit proof operation root mismatch");
        }
        if (record.proof->read_set_root != record.read_set_hash) {
            check.matched = false;
            check.diagnostics.push_back("commit proof read set root mismatch");
        }
        if (record.proof->write_set_root != record.write_set_hash) {
            check.matched = false;
            check.diagnostics.push_back("commit proof write set root mismatch");
        }
        if (record.proof->law_output_root != law_output_root(statuses, violations)) {
            check.matched = false;
            check.diagnostics.push_back("commit proof law output root mismatch");
        }
    } catch (const std::exception& error) {
        check.matched = false;
        check.diagnostics.push_back(std::string{"commit proof verification failed: "} + error.what());
    }
    return check;
}

ProgramReplayCheck check_program_replay(const Repository& repo, const CommitRecord& record) {
    ProgramReplayCheck check;
    check.commit = record.id;
    check.program_hash = record.program_hash;
    check.expected_delta_hash = record.delta_hash;
    check.matched = true;
    if (empty(record.program_hash)) {
        return check;
    }

    try {
        const auto stored = repo.objects().get_canonical<StoredCommit>(record.id.value);
        const auto before = repo.objects().get_canonical<WorldSnapshot>(stored.before_snapshot_object);
        if (stored.program_object != record.program_hash) {
            check.matched = false;
            check.diagnostics.push_back("commit program object mismatch");
            return check;
        }
        if (!repo.objects().contains(stored.program_object)) {
            check.matched = false;
            check.diagnostics.push_back("commit program object missing");
            return check;
        }
        const auto program = repo.objects().get_canonical<Program>(stored.program_object);
        if (program_hash(program) != record.program_hash) {
            check.matched = false;
            check.diagnostics.push_back("commit program hash mismatch");
        }
        if (instruction_root(program) != record.instruction_root) {
            check.matched = false;
            check.diagnostics.push_back("commit instruction root mismatch");
        }
        if (symbol_table_hash(program.symbols) != record.symbol_table_hash) {
            check.matched = false;
            check.diagnostics.push_back("commit symbol table hash mismatch");
        }
        const auto vm = KernelVm{}.execute(before, program);
        if (!vm.ok) {
            check.matched = false;
            check.diagnostics.push_back("commit program VM replay failed");
            return check;
        }
        check.observed_delta_hash = canonical_hash(vm.delta);
        if (check.observed_delta_hash != record.delta_hash) {
            check.matched = false;
            check.diagnostics.push_back("commit program delta replay mismatch");
        }
    } catch (const std::exception& error) {
        check.matched = false;
        check.diagnostics.push_back(std::string{"commit program replay failed: "} + error.what());
    }
    return check;
}

SentinelReport ProofPatrolWorker::run(const Repository& repo) const {
    SentinelReport report;
    for (const auto& ref : repo.list_branches()) {
        for (const auto& record : repo.history(ref.name)) {
            report.commits_checked += 1;
            const auto check = check_commit_proof(repo, record);
            if (!check.matched) {
                report.proof_mismatches += check.diagnostics.size();
                append_check_diagnostics(report, "ProofPatrolWorker", check.diagnostics, record.id);
            }
        }
    }
    report.measurement = report_measurement(report, "proof");
    return report;
}

SentinelReport VmReplayWorker::run(const Repository& repo, std::size_t max_programs) const {
    SentinelReport report;
    for (const auto& ref : repo.list_branches()) {
        for (auto history = repo.history(ref.name); const auto& record : history) {
            if (empty(record.program_hash)) {
                continue;
            }
            if (max_programs != 0 && report.program_replays >= max_programs) {
                report.measurement = report_measurement(report, "vm-replay");
                return report;
            }
            report.program_replays += 1;
            const auto check = check_program_replay(repo, record);
            if (!check.matched) {
                append_check_diagnostics(report, "VmReplayWorker", check.diagnostics, record.id);
            }
        }
    }
    report.measurement = report_measurement(report, "vm-replay");
    return report;
}

SentinelReport patrol_repository(const Repository& repo) {
    SentinelReport report = StorePatrolWorker{}.run(repo.root());
    RegionTable regions;
    const auto manifest_hash = hash_file_if_exists(repo.root() / "manifest.json");
    regions.add(IntegrityRegion{RegionKind::Manifest, "manifest.json", manifest_hash, manifest_hash, true});
    const auto opcode_hash = vm_opcode_table_root();
    regions.add(IntegrityRegion{RegionKind::VmOpcodeTable, "kernel-vm", opcode_hash, opcode_hash, true});

    std::set<std::string> known_commits;
    for (const auto& ref : repo.list_branches()) {
        report.branch_refs_checked += 1;
        regions.add(IntegrityRegion{
            RegionKind::BranchRef,
            ref.name,
            ref.snapshot,
            repo.world(ref.name).snapshot().canonical_hash(),
            true
        });
        if (!repo.objects().contains(ref.head.value)) {
            add_sentinel_error(report, "ProofPatrolWorker", "branch ref points to missing commit: " + ref.name);
        }
        if (!repo.objects().contains(ref.snapshot)) {
            add_sentinel_error(report, "ProofPatrolWorker", "branch ref points to missing snapshot: " + ref.name);
        }

        std::optional<CommitId> latest_accepted;
        for (const auto& record : repo.history(ref.name)) {
            known_commits.insert(to_hex(record.id.value));
            if (record.accepted) {
                latest_accepted = record.id;
            }
        }
        if (latest_accepted.has_value() && *latest_accepted != ref.head) {
            add_sentinel_error(report, "ProofPatrolWorker", "branch ref head does not match accepted history: " + ref.name);
        }
    }

    for (const auto& ref : repo.list_branches()) {
        for (const auto& record : repo.history(ref.name)) {
            report.commits_checked += 1;
            try {
                auto stored = repo.objects().get_canonical<StoredCommit>(record.id.value);
                stored.record.id = record.id;
                regions.add(IntegrityRegion{
                    RegionKind::CommitObject,
                    to_hex(record.id.value),
                    record.id.value,
                    sha256(repo.objects().get_bytes(record.id.value)),
                    true
                });
                if (make_commit_id(stored.record) != record.id) {
                    add_sentinel_error(report, "ProofPatrolWorker", "commit id does not match canonical record: " + to_hex(record.id.value));
                }
                for (const auto& parent : record.parents) {
                    if (!known_commits.contains(to_hex(parent.value)) && !repo.objects().contains(parent.value)) {
                        add_sentinel_error(report, "ProofPatrolWorker", "commit parent missing: " + to_hex(parent.value));
                    }
                }

                const auto before = repo.objects().get_canonical<WorldSnapshot>(stored.before_snapshot_object);
                const auto after = repo.objects().get_canonical<WorldSnapshot>(stored.after_snapshot_object);
                regions.add(IntegrityRegion{
                    RegionKind::SnapshotObject,
                    to_hex(record.id.value) + ":before",
                    record.before_hash,
                    before.canonical_hash(),
                    true
                });
                regions.add(IntegrityRegion{
                    RegionKind::SnapshotObject,
                    to_hex(record.id.value) + ":after",
                    record.after_hash,
                    after.canonical_hash(),
                    true
                });
                report.snapshots_checked += 2;
                if (before.canonical_hash() != record.before_hash) {
                    add_sentinel_error(report, "ProofPatrolWorker", "commit before snapshot hash mismatch: " + to_hex(record.id.value));
                }
                if (after.canonical_hash() != record.after_hash) {
                    add_sentinel_error(report, "ProofPatrolWorker", "commit after snapshot hash mismatch: " + to_hex(record.id.value));
                }

                const auto delta = repo.objects().get_canonical<Delta>(stored.delta_object);
                regions.add(IntegrityRegion{
                    RegionKind::DeltaObject,
                    to_hex(record.id.value),
                    record.delta_hash,
                    canonical_hash(delta),
                    true
                });
                if (canonical_hash(delta) != record.delta_hash) {
                    add_sentinel_error(report, "ProofPatrolWorker", "commit delta hash mismatch: " + to_hex(record.id.value));
                }
                const auto replay = check_program_replay(repo, record);
                if (!empty(record.program_hash)) {
                    report.program_replays += 1;
                }
                if (!replay.matched) {
                    append_check_diagnostics(report, "VmReplayWorker", replay.diagnostics, record.id);
                }

                const auto events = repo.objects().get_canonical<std::vector<TraceEvent>>(stored.trace_object);
                regions.add(IntegrityRegion{
                    RegionKind::TraceObject,
                    to_hex(record.id.value),
                    record.trace_hash,
                    canonical_hash(events),
                    true
                });
                if (canonical_hash(events) != record.trace_hash) {
                    add_sentinel_error(report, "ProofPatrolWorker", "commit trace hash mismatch: " + to_hex(record.id.value));
                }
                const auto statuses = repo.objects().get_canonical<std::vector<LawStatus>>(stored.law_status_object);
                regions.add(IntegrityRegion{
                    RegionKind::LawObject,
                    to_hex(record.id.value) + ":statuses",
                    record.law_hash,
                    canonical_hash(statuses),
                    true
                });
                if (canonical_hash(statuses) != record.law_hash) {
                    add_sentinel_error(report, "ProofPatrolWorker", "commit law hash mismatch: " + to_hex(record.id.value));
                }
                const auto violations = repo.objects().get_canonical<std::vector<LawViolation>>(stored.violation_object);
                regions.add(IntegrityRegion{
                    RegionKind::LawObject,
                    to_hex(record.id.value) + ":violations",
                    record.violation_hash,
                    canonical_hash(violations),
                    true
                });
                if (canonical_hash(violations) != record.violation_hash) {
                    add_sentinel_error(report, "ProofPatrolWorker", "commit violation hash mismatch: " + to_hex(record.id.value));
                }
                if (!empty(record.program_hash)) {
                    if (repo.objects().contains(stored.program_object)) {
                        const auto program = repo.objects().get_canonical<Program>(stored.program_object);
                        regions.add(IntegrityRegion{
                            RegionKind::ProgramObject,
                            to_hex(record.id.value),
                            record.program_hash,
                            program_hash(program),
                            true
                        });
                        regions.add(IntegrityRegion{
                            RegionKind::CompilerSymbolTable,
                            to_hex(record.id.value),
                            record.symbol_table_hash,
                            symbol_table_hash(program.symbols),
                            true
                        });
                    } else {
                        regions.add(IntegrityRegion{
                            RegionKind::ProgramObject,
                            to_hex(record.id.value),
                            record.program_hash,
                            Hash256{},
                            true
                        });
                    }
                }

                const auto proof = check_commit_proof(repo, record);
                if (record.proof.has_value()) {
                    regions.add(IntegrityRegion{
                        RegionKind::ProofObject,
                        to_hex(record.id.value),
                        record.proof_hash,
                        hash_commit_proof(*record.proof),
                        true
                    });
                }
                if (!proof.matched) {
                    report.proof_mismatches += proof.diagnostics.size();
                    append_check_diagnostics(report, "ProofPatrolWorker", proof.diagnostics, record.id);
                }
            } catch (const std::exception& error) {
                add_sentinel_error(
                    report,
                    "ProofPatrolWorker",
                    "commit verification failed for " + to_hex(record.id.value) + ": " + error.what());
            }
        }
    }

    const auto region_report = regions.verify();
    report.regions_checked += region_report.regions_checked;
    for (const auto& error : region_report.errors) {
        add_sentinel_error(report, "RegionTable", error);
    }
    for (const auto& warning : region_report.warning_messages) {
        add_sentinel_warning(report, "RegionTable", warning);
    }
    report.measurement = report_measurement(report, "repository");
    return report;
}

}  // namespace pv
