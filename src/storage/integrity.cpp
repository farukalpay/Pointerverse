// SPDX-License-Identifier: Apache-2.0
#include "pv/storage/integrity.hpp"

#include <fstream>
#include <iterator>
#include <set>
#include <utility>

#include "pv/hash/hasher.hpp"
#include "pv/storage/repository.hpp"

namespace pv {
namespace {

std::vector<std::byte> read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::vector<std::byte> out;
    for (std::istreambuf_iterator<char> iter{input}, end; iter != end; ++iter) {
        out.push_back(static_cast<std::byte>(static_cast<unsigned char>(*iter)));
    }
    return out;
}

void add_error(IntegrityReport& report, std::string message) {
    report.errors.push_back(IntegrityError{std::move(message)});
}

}  // namespace

IntegrityReport IntegrityChecker::check_repository(const Repository& repo) const {
    IntegrityReport report;

    const auto object_root = repo.objects().root() / "objects";
    if (std::filesystem::exists(object_root)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(object_root)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            report.objects_checked += 1;
            const auto filename = entry.path().filename().string();
            const auto parsed = parse_hash256(filename);
            if (!parsed.has_value()) {
                add_error(report, "object has invalid hash filename: " + entry.path().string());
                continue;
            }
            const auto bytes = read_bytes(entry.path());
            if (sha256(bytes) != *parsed) {
                add_error(report, "object blob hash does not match filename: " + filename);
            }
        }
    }

    std::set<std::string> known_commits;
    for (const auto& ref : repo.list_branches()) {
        report.branch_refs_checked += 1;
        if (!repo.objects().contains(ref.head.value)) {
            add_error(report, "branch ref points to missing commit: " + ref.name);
        }
        if (!repo.objects().contains(ref.snapshot)) {
            add_error(report, "branch ref points to missing snapshot: " + ref.name);
        }

        for (const auto& record : repo.history(ref.name)) {
            known_commits.insert(to_hex(record.id.value));
        }
    }

    for (const auto& ref : repo.list_branches()) {
        for (const auto& record : repo.history(ref.name)) {
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
            } catch (const std::exception& error) {
                add_error(report, "commit verification failed for " + to_hex(record.id.value) + ": " + error.what());
            }
        }
    }

    return report;
}

}  // namespace pv
