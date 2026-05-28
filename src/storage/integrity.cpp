// SPDX-License-Identifier: Apache-2.0
#include "pv/storage/integrity.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>

#include "pv/hash/hasher.hpp"
#include "pv/kernel/canonical_codec.hpp"
#include "pv/kernel/merkle.hpp"
#include "pv/kernel/program.hpp"
#include "pv/kernel/proof.hpp"
#include "pv/kernel/vm.hpp"
#include "pv/storage/repository.hpp"
#include "pv/storage/object_codec.hpp"

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

void add_error(IntegrityReport& report, std::string message) {
    report.errors.push_back(IntegrityError{std::move(message)});
}

Hash256 law_output_root(const std::vector<LawStatus>& statuses, const std::vector<LawViolation>& violations) {
    CanonicalWriter writer;
    writer.string("LawOutputRoot:v1");
    writer.u64(2);
    writer.hash(canonical_hash(statuses));
    writer.hash(canonical_hash(violations));
    return sha256(writer.bytes());
}

void check_object_store(const Repository& repo, IntegrityReport& report) {
    const auto object_root = repo.root() / "objects";
    if (!std::filesystem::exists(object_root)) {
        return;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(object_root)) {
        if (!entry.is_regular_file() || entry.path().extension() == ".tmp") {
            continue;
        }
        report.objects_checked += 1;
        const auto filename = entry.path().filename().string();
        const auto expected = parse_hash256(filename);
        if (!expected.has_value()) {
            add_error(report, "object has invalid hash filename: " + entry.path().string());
            continue;
        }
        try {
            const auto actual = sha256(read_raw_bytes(entry.path()));
            if (actual != *expected) {
                add_error(report, "object blob hash does not match filename: " + filename);
            }
        } catch (const std::exception& error) {
            add_error(report, error.what());
        }
    }
}

void check_program_replay(const Repository& repo, const StoredCommit& stored, const CommitRecord& record, IntegrityReport& report) {
    if (empty(record.program_hash)) {
        return;
    }
    if (stored.program_object != record.program_hash) {
        add_error(report, "commit program object mismatch: " + to_hex(record.id.value));
        return;
    }
    if (!repo.objects().contains(stored.program_object)) {
        add_error(report, "commit program object missing: " + to_hex(record.id.value));
        return;
    }
    const auto before = repo.objects().get_canonical<WorldSnapshot>(stored.before_snapshot_object);
    const auto program = repo.objects().get_canonical<Program>(stored.program_object);
    if (program_hash(program) != record.program_hash) {
        add_error(report, "commit program hash mismatch: " + to_hex(record.id.value));
    }
    if (instruction_root(program) != record.instruction_root) {
        add_error(report, "commit instruction root mismatch: " + to_hex(record.id.value));
    }
    if (symbol_table_hash(program.symbols) != record.symbol_table_hash) {
        add_error(report, "commit symbol table hash mismatch: " + to_hex(record.id.value));
    }
    const auto vm = KernelVm{}.execute(before, program);
    if (!vm.ok) {
        add_error(report, "commit program VM replay failed: " + to_hex(record.id.value));
        return;
    }
    if (canonical_hash(vm.delta) != record.delta_hash) {
        add_error(report, "commit program delta replay mismatch: " + to_hex(record.id.value));
    }
}

void check_commit_proof(
    const Repository& repo,
    const StoredCommit& stored,
    const CommitRecord& record,
    const std::vector<LawStatus>& statuses,
    const std::vector<LawViolation>& violations,
    IntegrityReport& report) {
    if (!record.proof.has_value()) {
        return;
    }

    const auto before = repo.objects().get_canonical<WorldSnapshot>(stored.before_snapshot_object);
    const auto after = repo.objects().get_canonical<WorldSnapshot>(stored.after_snapshot_object);
    const auto proof_hash = hash_commit_proof(*record.proof);
    if (proof_hash != record.proof_hash) {
        add_error(report, "commit proof hash mismatch: " + to_hex(record.id.value));
    }
    if (record.proof->before_root != compute_world_root(before).root) {
        add_error(report, "commit proof before root mismatch: " + to_hex(record.id.value));
    }
    if (record.proof->program_root != record.program_hash) {
        add_error(report, "commit proof program root mismatch: " + to_hex(record.id.value));
    }
    if (record.proof->after_root != compute_world_root(after).root) {
        add_error(report, "commit proof after root mismatch: " + to_hex(record.id.value));
    }
    if (record.proof->operation_root != record.delta_hash) {
        add_error(report, "commit proof operation root mismatch: " + to_hex(record.id.value));
    }
    if (record.proof->read_set_root != record.read_set_hash) {
        add_error(report, "commit proof read set root mismatch: " + to_hex(record.id.value));
    }
    if (record.proof->write_set_root != record.write_set_hash) {
        add_error(report, "commit proof write set root mismatch: " + to_hex(record.id.value));
    }
    if (record.proof->law_output_root != law_output_root(statuses, violations)) {
        add_error(report, "commit proof law output root mismatch: " + to_hex(record.id.value));
    }
}

void check_commit(
    const Repository& repo,
    const CommitRecord& record,
    const std::set<std::string>& known_commits,
    IntegrityReport& report) {
    report.commits_checked += 1;
    try {
        auto stored = repo.objects().get_canonical<StoredCommit>(record.id.value);
        stored.record.id = record.id;
        if (make_commit_id(stored.record) != record.id) {
            add_error(report, "commit id does not match canonical record: " + to_hex(record.id.value));
        }
        for (const auto& parent : record.parents) {
            if (!known_commits.contains(to_hex(parent.value)) && !repo.objects().contains(parent.value)) {
                add_error(report, "commit parent missing: " + to_hex(parent.value));
            }
        }

        const auto before = repo.objects().get_canonical<WorldSnapshot>(stored.before_snapshot_object);
        const auto after = repo.objects().get_canonical<WorldSnapshot>(stored.after_snapshot_object);
        report.snapshots_checked += 2;
        if (before.canonical_hash() != record.before_hash) {
            add_error(report, "commit before snapshot hash mismatch: " + to_hex(record.id.value));
        }
        if (after.canonical_hash() != record.after_hash) {
            add_error(report, "commit after snapshot hash mismatch: " + to_hex(record.id.value));
        }

        const auto delta = repo.objects().get_canonical<Delta>(stored.delta_object);
        if (canonical_hash(delta) != record.delta_hash) {
            add_error(report, "commit delta hash mismatch: " + to_hex(record.id.value));
        }
        const auto events = repo.objects().get_canonical<std::vector<TraceEvent>>(stored.trace_object);
        if (canonical_hash(events) != record.trace_hash) {
            add_error(report, "commit trace hash mismatch: " + to_hex(record.id.value));
        }
        const auto statuses = repo.objects().get_canonical<std::vector<LawStatus>>(stored.law_status_object);
        if (canonical_hash(statuses) != record.law_hash) {
            add_error(report, "commit law hash mismatch: " + to_hex(record.id.value));
        }
        const auto violations = repo.objects().get_canonical<std::vector<LawViolation>>(stored.violation_object);
        if (canonical_hash(violations) != record.violation_hash) {
            add_error(report, "commit violation hash mismatch: " + to_hex(record.id.value));
        }

        check_program_replay(repo, stored, record, report);
        check_commit_proof(repo, stored, record, statuses, violations, report);
    } catch (const std::exception& error) {
        add_error(report, "commit verification failed for " + to_hex(record.id.value) + ": " + error.what());
    }
}

}  // namespace

IntegrityReport IntegrityChecker::check_repository(const Repository& repo) const {
    IntegrityReport report;
    check_object_store(repo, report);

    std::set<std::string> known_commits;
    for (const auto& ref : repo.list_branches()) {
        for (const auto& record : repo.history(ref.name)) {
            known_commits.insert(to_hex(record.id.value));
        }
    }

    for (const auto& ref : repo.list_branches()) {
        report.branch_refs_checked += 1;
        if (!repo.objects().contains(ref.head.value)) {
            add_error(report, "branch ref points to missing commit: " + ref.name);
        }
        if (!repo.objects().contains(ref.snapshot)) {
            add_error(report, "branch ref points to missing snapshot: " + ref.name);
        }
        if (repo.world(ref.name).snapshot().canonical_hash() != ref.snapshot) {
            add_error(report, "branch ref snapshot mismatch: " + ref.name);
        }

        std::optional<CommitId> latest_accepted;
        for (const auto& record : repo.history(ref.name)) {
            if (record.accepted) {
                latest_accepted = record.id;
            }
            check_commit(repo, record, known_commits, report);
        }
        if (latest_accepted.has_value() && *latest_accepted != ref.head) {
            add_error(report, "branch ref head does not match accepted history: " + ref.name);
        }
    }
    return report;
}

}  // namespace pv
