// SPDX-License-Identifier: Apache-2.0
#include "pv/sentinel/sentinel.hpp"

#include <utility>
#include <optional>
#include <string>

#include "pv/hash/hasher.hpp"
#include "pv/sentinel/patrol.hpp"
#include "pv/storage/canonical_codec.hpp"
#include "pv/storage/repository.hpp"

namespace pv {
namespace {

Hash256 heartbeat_measurement(const SentinelReport& report, std::string_view worker) {
    CanonicalWriter writer;
    writer.string("PointerverseHeartbeat:v1");
    writer.string(worker);
    writer.hash(report.measurement);
    writer.u64(report.issues.size());
    writer.u8(report.clean() ? 1 : 0);
    return sha256(writer.bytes());
}

}  // namespace

SentinelRuntime::SentinelRuntime(std::filesystem::path root) : root_(std::move(root)) {}

SentinelReport SentinelRuntime::run_once() {
    const auto store = StorePatrolWorker{}.run(root_);

    SentinelReport report;
    std::optional<Repository> repo;
    if (store.clean()) {
        try {
            repo.emplace(Repository::open(root_));
        } catch (const std::exception& error) {
            add_sentinel_error(report, "SentinelRuntime", std::string{"repository open failed: "} + error.what());
        }
    }

    SentinelReport proof;
    SentinelReport replay;
    if (repo.has_value()) {
        proof = ProofPatrolWorker{}.run(*repo);
        replay = VmReplayWorker{}.run(*repo);
        report = patrol_repository(*repo);
    } else {
        merge_into(report, store);
        add_sentinel_error(proof, "ProofPatrolWorker", "repository was not available for proof patrol");
        add_sentinel_error(replay, "VmReplayWorker", "repository was not available for VM replay patrol");
        merge_into(report, proof);
        merge_into(report, replay);
    }

    report.heartbeats.push_back(Heartbeat{"StorePatrolWorker", tick_, heartbeat_measurement(store, "store"), store.clean()});
    report.heartbeats.push_back(Heartbeat{"ProofPatrolWorker", tick_, heartbeat_measurement(proof, "proof"), proof.clean()});
    report.heartbeats.push_back(Heartbeat{"VmReplayWorker", tick_, heartbeat_measurement(replay, "vm-replay"), replay.clean()});
    heartbeats_ = report.heartbeats;

    CanonicalWriter writer;
    writer.string("PointerverseSentinelRuntime:v1");
    writer.hash(store.measurement);
    writer.hash(proof.measurement);
    writer.hash(replay.measurement);
    writer.u64(tick_);
    report.measurement = sha256(writer.bytes());
    return report;
}

SentinelReport SentinelRuntime::tick() {
    tick_ += 1;
    return run_once();
}

const std::vector<Heartbeat>& SentinelRuntime::heartbeats() const noexcept {
    return heartbeats_;
}

}  // namespace pv
