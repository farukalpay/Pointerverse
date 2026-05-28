// SPDX-License-Identifier: Apache-2.0
#include "pv/sentinel/boot_gate.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <fmt/format.h>

#include "pv/hash/hasher.hpp"
#include "pv/kernel/program.hpp"
#include "pv/sentinel/patrol.hpp"
#include "pv/kernel/canonical_codec.hpp"
#include "pv/storage/content_store.hpp"
#include "pv/storage/manifest.hpp"
#include "pv/storage/ref_store.hpp"
#include "pv/storage/repository.hpp"

namespace pv {
namespace {

std::vector<std::byte> read_bytes_if_exists(const std::filesystem::path& path) {
    std::vector<std::byte> out;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return out;
    }
    for (std::istreambuf_iterator<char> iter{input}, end; iter != end; ++iter) {
        out.push_back(static_cast<std::byte>(static_cast<unsigned char>(*iter)));
    }
    return out;
}

Hash256 hash_file_if_exists(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return Hash256{};
    }
    return sha256(read_bytes_if_exists(path));
}

Hash256 hash_directory_files(const std::filesystem::path& root) {
    CanonicalWriter writer;
    writer.string("PointerverseDirectory:v1");
    std::vector<std::pair<std::string, Hash256>> files;
    if (std::filesystem::exists(root)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            if (entry.path().extension() == ".tmp") {
                continue;
            }
            files.push_back({
                entry.path().lexically_relative(root).generic_string(),
                sha256(read_bytes_if_exists(entry.path()))
            });
        }
    }
    std::ranges::sort(files, [](const auto& left, const auto& right) {
        return left.first < right.first;
    });
    writer.u64(files.size());
    for (const auto& [path, hash] : files) {
        writer.string(path);
        writer.hash(hash);
    }
    return sha256(writer.bytes());
}

Hash256 opcode_table_root() {
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

Hash256 branch_root(const Repository& repo) {
    CanonicalWriter writer;
    writer.string("PointerverseBranchRoot:v1");
    const auto refs = repo.list_branches();
    writer.u64(refs.size());
    for (const auto& ref : refs) {
        writer.string(ref.name);
        writer.u64(ref.branch.value);
        writer.hash(ref.head.value);
        writer.hash(ref.snapshot);
        writer.u64(ref.epoch.value);
    }
    return sha256(writer.bytes());
}

Hash256 commit_graph_root(const Repository& repo) {
    CanonicalWriter writer;
    writer.string("PointerverseCommitGraphRoot:v1");
    const auto refs = repo.list_branches();
    writer.u64(refs.size());
    for (const auto& ref : refs) {
        writer.string(ref.name);
        const auto history = repo.history(ref.name);
        writer.u64(history.size());
        for (const auto& record : history) {
            writer.hash(record.id.value);
            writer.u64(record.parents.size());
            for (const auto& parent : record.parents) {
                writer.hash(parent.value);
            }
            writer.hash(record.before_hash);
            writer.hash(record.after_hash);
            writer.hash(record.delta_hash);
            writer.hash(record.program_hash);
            writer.hash(record.proof_hash);
            writer.u8(record.accepted ? 1 : 0);
        }
    }
    return sha256(writer.bytes());
}

Hash256 latest_world_root(const Repository& repo) {
    CanonicalWriter writer;
    writer.string("PointerverseLatestWorldRoot:v1");
    const auto refs = repo.list_branches();
    writer.u64(refs.size());
    for (const auto& ref : refs) {
        writer.string(ref.name);
        writer.hash(repo.world(ref.name).snapshot().canonical_hash());
    }
    return sha256(writer.bytes());
}

Hash256 sentinel_root() {
    CanonicalWriter writer;
    writer.string("PointerverseSentinelRoot:v1");
    writer.hash(opcode_table_root());
    writer.string("boot-gate");
    writer.string("region-table");
    writer.string("store-patrol");
    writer.string("proof-patrol");
    writer.string("vm-replay");
    writer.string("heartbeat");
    return sha256(writer.bytes());
}

BootMeasurement measure_boot(const std::filesystem::path& root, const Repository& repo) {
    BootMeasurement measurement;
    measurement.manifest_root = hash_file_if_exists(root / "manifest.json");
    measurement.ref_root = hash_directory_files(root / "refs");
    measurement.branch_root = branch_root(repo);
    measurement.commit_graph_root = commit_graph_root(repo);
    measurement.latest_world_root = latest_world_root(repo);
    measurement.sentinel_root = sentinel_root();

    CanonicalWriter writer;
    writer.string("PointerverseBootMeasurement:v1");
    writer.hash(measurement.manifest_root);
    writer.hash(measurement.ref_root);
    writer.hash(measurement.branch_root);
    writer.hash(measurement.commit_graph_root);
    writer.hash(measurement.latest_world_root);
    writer.hash(measurement.sentinel_root);
    measurement.root = sha256(writer.bytes());
    return measurement;
}

std::optional<Hash256> read_previous_measurement(const std::filesystem::path& root) {
    std::ifstream input(root / "sentinel" / "last_boot");
    if (!input) {
        return std::nullopt;
    }
    std::string key;
    std::string value;
    while (input >> key >> value) {
        if (key == "root") {
            return parse_hash256(value);
        }
    }
    return std::nullopt;
}

void write_boot_measurement(const std::filesystem::path& root, const BootMeasurement& measurement) {
    const auto sentinel_dir = root / "sentinel";
    std::filesystem::create_directories(sentinel_dir);
    const auto path = sentinel_dir / "last_boot";
    const auto tmp = sentinel_dir / "last_boot.tmp";
    std::ofstream output(tmp, std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot write sentinel boot measurement");
    }
    output << "version 1\n";
    output << "root " << to_hex(measurement.root) << '\n';
    output << "manifest_root " << to_hex(measurement.manifest_root) << '\n';
    output << "ref_root " << to_hex(measurement.ref_root) << '\n';
    output << "branch_root " << to_hex(measurement.branch_root) << '\n';
    output << "commit_graph_root " << to_hex(measurement.commit_graph_root) << '\n';
    output << "latest_world_root " << to_hex(measurement.latest_world_root) << '\n';
    output << "sentinel_root " << to_hex(measurement.sentinel_root) << '\n';
    output.close();
    std::filesystem::rename(tmp, path);
}

void append_issues(BootGateResult& result, const SentinelReport& report) {
    for (const auto& issue : report.issues) {
        result.diagnostics.push_back(issue.message);
    }
}

BootGateResult fail(BootStage stage, std::string message) {
    BootGateResult result;
    result.ok = false;
    result.failed_at = stage;
    result.diagnostics.push_back(std::move(message));
    return result;
}

}  // namespace

std::string to_string(BootStage stage) {
    switch (stage) {
    case BootStage::Manifest:
        return "Manifest";
    case BootStage::ObjectStore:
        return "ObjectStore";
    case BootStage::BranchRefs:
        return "BranchRefs";
    case BootStage::CommitGraph:
        return "CommitGraph";
    case BootStage::SnapshotLoad:
        return "SnapshotLoad";
    case BootStage::VmReplay:
        return "VmReplay";
    case BootStage::ProofChain:
        return "ProofChain";
    case BootStage::Ready:
        return "Ready";
    }
    return "Manifest";
}

BootGateResult run_boot_gate(const std::filesystem::path& root) {
    try {
        ManifestStore manifest{root};
        if (!manifest.exists()) {
            return fail(BootStage::Manifest, "not a Pointerverse repository");
        }
        (void)manifest.read();
    } catch (const std::exception& error) {
        return fail(BootStage::Manifest, error.what());
    }

    {
        const auto store = StorePatrolWorker{}.run(root);
        if (!store.clean()) {
            auto result = fail(BootStage::ObjectStore, "object store verification failed");
            append_issues(result, store);
            return result;
        }
    }

    try {
        ContentStore objects{root};
        RefStore refs{root};
        for (const auto& ref : refs.list_branches()) {
            if (!objects.contains(ref.head.value)) {
                return fail(BootStage::BranchRefs, "branch ref points to missing commit: " + ref.name);
            }
            if (!objects.contains(ref.snapshot)) {
                return fail(BootStage::BranchRefs, "branch ref points to missing snapshot: " + ref.name);
            }
        }
    } catch (const std::exception& error) {
        return fail(BootStage::BranchRefs, error.what());
    }

    std::optional<Repository> repo;
    try {
        repo.emplace(Repository::open(root));
    } catch (const std::exception& error) {
        return fail(BootStage::CommitGraph, error.what());
    }

    try {
        for (const auto& ref : repo->list_branches()) {
            const auto observed = repo->world(ref.name).snapshot().canonical_hash();
            if (observed != ref.snapshot) {
                return fail(BootStage::SnapshotLoad, "latest snapshot hash mismatch: " + ref.name);
            }
            std::optional<Hash256> latest_accepted_snapshot;
            for (const auto& record : repo->history(ref.name)) {
                if (record.accepted) {
                    latest_accepted_snapshot = record.after_hash;
                }
            }
            if (latest_accepted_snapshot.has_value() && *latest_accepted_snapshot != ref.snapshot) {
                return fail(BootStage::SnapshotLoad, "branch ref snapshot does not match accepted history: " + ref.name);
            }
        }
    } catch (const std::exception& error) {
        return fail(BootStage::SnapshotLoad, error.what());
    }

    {
        const auto replay = VmReplayWorker{}.run(*repo, 1);
        if (!replay.clean()) {
            auto result = fail(BootStage::VmReplay, "VM replay sample failed");
            append_issues(result, replay);
            return result;
        }
    }

    {
        const auto proof = ProofPatrolWorker{}.run(*repo);
        if (!proof.clean()) {
            auto result = fail(BootStage::ProofChain, "proof-chain verification failed");
            append_issues(result, proof);
            return result;
        }
    }

    BootGateResult result;
    result.ok = true;
    result.failed_at = BootStage::Ready;
    result.measurement = measure_boot(root, *repo);
    if (const auto previous = read_previous_measurement(root); previous.has_value() && *previous != result.measurement.root) {
        result.diagnostics.push_back(
            "boot measurement changed: previous "
            + to_hex(*previous)
            + ", current "
            + to_hex(result.measurement.root));
    }
    write_boot_measurement(root, result.measurement);
    return result;
}

Repository open_repository_with_sentinel(std::filesystem::path root, BootGateResult* result) {
    auto boot = run_boot_gate(root);
    if (result != nullptr) {
        *result = boot;
    }
    if (!boot.ok) {
        throw std::runtime_error("sentinel boot failed at " + to_string(boot.failed_at));
    }
    return Repository::open(std::move(root));
}

std::string render_boot_gate_result(const BootGateResult& result) {
    std::ostringstream output;
    output << "Pointerverse Sentinel Boot\n";
    output << "--------------------------\n";
    output << fmt::format("boot:              {}\n", result.ok ? "clean" : "errors");
    output << fmt::format("stage:             {}\n", to_string(result.failed_at));
    output << fmt::format("measurement:       {}\n", to_hex(result.measurement.root));
    output << fmt::format("manifest root:     {}\n", to_hex(result.measurement.manifest_root));
    output << fmt::format("ref root:          {}\n", to_hex(result.measurement.ref_root));
    output << fmt::format("branch root:       {}\n", to_hex(result.measurement.branch_root));
    output << fmt::format("commit graph root: {}\n", to_hex(result.measurement.commit_graph_root));
    output << fmt::format("latest world root: {}\n", to_hex(result.measurement.latest_world_root));
    output << fmt::format("sentinel root:     {}\n", to_hex(result.measurement.sentinel_root));
    for (const auto& diagnostic : result.diagnostics) {
        output << fmt::format("{}: {}\n", result.ok ? "note" : "error", diagnostic);
    }
    return output.str();
}

}  // namespace pv
