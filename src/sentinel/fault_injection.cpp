// SPDX-License-Identifier: Apache-2.0
#include "pv/sentinel/fault_injection.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "pv/hash/hasher.hpp"
#include "pv/kernel/proof.hpp"
#include "pv/kernel/canonical_codec.hpp"
#include "pv/storage/content_store.hpp"
#include "pv/storage/object_codec.hpp"
#include "pv/storage/ref_store.hpp"
#include "pv/storage/repository.hpp"

namespace pv {
namespace {

void require_confirmation(const FaultInjectionOptions& options) {
    if (!options.confirm_mutates_store) {
        throw std::runtime_error("fault injection requires --yes-i-know-this-mutates-store");
    }
}

std::vector<std::byte> read_raw_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open '" + path.string() + "'");
    }
    std::vector<std::byte> out;
    for (std::istreambuf_iterator<char> iter{input}, end; iter != end; ++iter) {
        out.push_back(static_cast<std::byte>(static_cast<unsigned char>(*iter)));
    }
    return out;
}

void write_raw_bytes(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot write '" + path.string() + "'");
    }
    for (const auto byte : bytes) {
        output.put(static_cast<char>(byte));
    }
}

Hash256 fault_hash(std::uint8_t seed) {
    CanonicalWriter writer;
    writer.string("PointerverseFaultHash:v1");
    writer.u8(seed);
    return sha256(writer.bytes());
}

std::string selected_branch(const Repository& repo, const FaultInjectionOptions& options) {
    return options.branch.empty() ? repo.current_branch() : options.branch;
}

CommitId resolve_commit(const Repository& repo, std::string_view branch, std::string_view spec) {
    if (spec.empty() || spec == "HEAD") {
        const auto ref = repo.refs().read_branch(branch);
        if (!ref.has_value()) {
            throw std::runtime_error("unknown branch '" + std::string{branch} + "'");
        }
        return ref->head;
    }

    if (const auto parsed = parse_hash256(spec); parsed.has_value()) {
        return CommitId{*parsed};
    }

    std::optional<CommitId> matched;
    for (const auto& record : repo.history(branch)) {
        const auto hex = to_hex(record.id.value);
        if (hex.starts_with(spec)) {
            if (matched.has_value()) {
                throw std::runtime_error("ambiguous commit prefix '" + std::string{spec} + "'");
            }
            matched = record.id;
        }
    }
    if (!matched.has_value()) {
        throw std::runtime_error("unknown commit '" + std::string{spec} + "'");
    }
    return *matched;
}

CommitRecord require_record(const Repository& repo, std::string_view branch, CommitId id) {
    for (const auto& record : repo.history(branch)) {
        if (record.id == id) {
            return record;
        }
    }
    throw std::runtime_error("commit is not in branch history: " + to_hex(id.value));
}

Hash256 object_for_kind(const Repository& repo, const CommitRecord& record, FaultObjectKind kind) {
    const auto stored = repo.objects().get_canonical<StoredCommit>(record.id.value);
    switch (kind) {
    case FaultObjectKind::Snapshot:
        return stored.after_snapshot_object;
    case FaultObjectKind::Commit:
    case FaultObjectKind::Proof:
        return record.id.value;
    case FaultObjectKind::Program:
        if (empty(stored.program_object)) {
            throw std::runtime_error("selected commit has no program object");
        }
        return stored.program_object;
    case FaultObjectKind::Delta:
        return stored.delta_object;
    case FaultObjectKind::Trace:
        return stored.trace_object;
    case FaultObjectKind::Law:
        return stored.law_status_object;
    }
    return record.id.value;
}

std::filesystem::path history_path(const std::filesystem::path& root, std::string_view branch) {
    if (!RefStore::valid_branch_name(branch)) {
        throw std::invalid_argument("invalid branch name");
    }
    return root / "history" / "branches" / std::filesystem::path{std::string{branch}};
}

void rewrite_history_id(
    const std::filesystem::path& root,
    std::string_view branch,
    CommitId old_id,
    CommitId new_id) {
    const auto path = history_path(root, branch);
    std::vector<std::string> lines;
    {
        std::ifstream input(path);
        std::string line;
        while (std::getline(input, line)) {
            if (line == to_hex(old_id.value)) {
                line = to_hex(new_id.value);
            }
            if (!line.empty()) {
                lines.push_back(std::move(line));
            }
        }
    }

    std::filesystem::create_directories(path.parent_path());
    const auto tmp = path.string() + ".tmp";
    std::ofstream output(tmp, std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot rewrite branch history");
    }
    for (const auto& line : lines) {
        output << line << '\n';
    }
    output.close();
    std::filesystem::rename(tmp, path);
}

}  // namespace

FaultObjectKind parse_fault_object_kind(std::string_view text) {
    if (text == "snapshot") {
        return FaultObjectKind::Snapshot;
    }
    if (text == "commit") {
        return FaultObjectKind::Commit;
    }
    if (text == "program") {
        return FaultObjectKind::Program;
    }
    if (text == "delta") {
        return FaultObjectKind::Delta;
    }
    if (text == "trace") {
        return FaultObjectKind::Trace;
    }
    if (text == "law") {
        return FaultObjectKind::Law;
    }
    if (text == "proof") {
        return FaultObjectKind::Proof;
    }
    throw std::invalid_argument("fault object kind must be snapshot, commit, program, delta, trace, law, or proof");
}

FaultInjectionResult corrupt_object_fault(const FaultInjectionOptions& options) {
    require_confirmation(options);
    auto repo = Repository::open(options.root);
    const auto branch = selected_branch(repo, options);
    const auto commit = resolve_commit(repo, branch, options.commit);
    const auto record = require_record(repo, branch, commit);
    const auto object = object_for_kind(repo, record, options.kind);
    const auto path = repo.objects().object_path(object);
    auto bytes = read_raw_bytes(path);
    if (bytes.empty()) {
        bytes.push_back(std::byte{0x42});
    } else {
        bytes.front() ^= std::byte{0xff};
    }
    write_raw_bytes(path, bytes);
    return FaultInjectionResult{true, to_hex(object), "corrupted object blob"};
}

FaultInjectionResult remove_program_fault(const FaultInjectionOptions& options) {
    require_confirmation(options);
    auto repo = Repository::open(options.root);
    const auto branch = selected_branch(repo, options);
    const auto commit = resolve_commit(repo, branch, options.commit);
    const auto record = require_record(repo, branch, commit);
    const auto stored = repo.objects().get_canonical<StoredCommit>(record.id.value);
    if (empty(stored.program_object)) {
        throw std::runtime_error("selected commit has no program object");
    }
    const auto path = repo.objects().object_path(stored.program_object);
    if (!std::filesystem::remove(path)) {
        throw std::runtime_error("program object was not removed");
    }
    return FaultInjectionResult{true, to_hex(stored.program_object), "removed program object"};
}

FaultInjectionResult rewrite_ref_fault(const FaultInjectionOptions& options) {
    require_confirmation(options);
    auto repo = Repository::open(options.root);
    const auto branch = selected_branch(repo, options);
    auto ref = repo.refs().read_branch(branch);
    if (!ref.has_value()) {
        throw std::runtime_error("unknown branch '" + branch + "'");
    }
    ref->head = CommitId{fault_hash(0x51)};
    ref->snapshot = fault_hash(0x52);
    repo.refs().update_branch(*ref);
    return FaultInjectionResult{true, branch, "rewrote branch ref to missing commit and snapshot"};
}

FaultInjectionResult flip_proof_fault(const FaultInjectionOptions& options) {
    require_confirmation(options);
    auto repo = Repository::open(options.root);
    const auto branch = selected_branch(repo, options);
    const auto old_id = resolve_commit(repo, branch, options.commit);
    auto ref = repo.refs().read_branch(branch);
    if (!ref.has_value()) {
        throw std::runtime_error("unknown branch '" + branch + "'");
    }
    if (ref->head != old_id) {
        throw std::runtime_error("flip-proof v1 only rewrites the branch head commit");
    }

    auto stored = repo.objects().get_canonical<StoredCommit>(old_id.value);
    stored.record.id = old_id;
    if (!stored.record.proof.has_value()) {
        throw std::runtime_error("selected commit has no proof");
    }
    stored.record.proof->operation_root = fault_hash(0x71);
    stored.record.proof_hash = hash_commit_proof(*stored.record.proof);
    stored.record.id = make_commit_id(stored.record);

    const auto new_object = repo.objects().put_canonical(stored);
    if (new_object != stored.record.id.value) {
        throw std::runtime_error("rewritten proof object did not hash to rewritten commit id");
    }

    ref->head = stored.record.id;
    repo.refs().update_branch(*ref);
    rewrite_history_id(options.root, branch, old_id, stored.record.id);
    return FaultInjectionResult{true, to_hex(stored.record.id.value), "rewrote head commit with invalid proof roots"};
}

}  // namespace pv
