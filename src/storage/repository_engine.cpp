// SPDX-License-Identifier: Apache-2.0
#include "pv/storage/repository_engine.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <utility>

#include "pv/hash/canonical.hpp"
#include "pv/storage/object_codec.hpp"
#include "pv/storage/pack_store.hpp"
#include "pv/storage/snapshot_materializer.hpp"

namespace pv {
namespace {

std::filesystem::path history_path(const std::filesystem::path& root, std::string_view branch) {
    if (!RefStore::valid_branch_name(branch)) {
        throw std::invalid_argument("invalid branch name");
    }
    return root / "history" / "branches" / std::filesystem::path{std::string{branch}};
}

std::string commit_key(CommitId id) {
    return to_hex(id.value);
}

bool contains_commit(const std::vector<CommitId>& ids, CommitId id) {
    return std::ranges::find(ids, id) != ids.end();
}

bool index_ok(const IndexFileStatus& status) {
    return status.exists && status.checksum_ok;
}

std::size_t count_content_objects(const std::filesystem::path& root) {
    const auto object_root = root / "objects";
    std::size_t count = 0;
    if (std::filesystem::exists(object_root)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(object_root)) {
            if (entry.is_regular_file() && entry.path().extension() != ".tmp") {
                count += 1;
            }
        }
    }
    return count + PackedContentStore{root}.packed_object_count();
}

}  // namespace

RepositoryEngine::RepositoryEngine(
    std::filesystem::path root,
    ContentStore& objects,
    RefStore& refs,
    Wal& wal)
    : root_(std::move(root)),
      objects_(objects),
      refs_(refs),
      wal_(wal),
      commits_(root_),
      branches_(root_),
      world_index_(root_),
      events_(root_) {}

void RepositoryEngine::open() {
    const auto wal_report = wal_.recover();
    (void)wal_report;
    const auto refs = refs_.list_branches();
    if (!commits_.exists() || !branches_.exists() || !world_index_.exists() || !events_.exists()) {
        rebuild_indexes();
        return;
    }
    const auto check = check_indexes();
    if (!check.clean) {
        rebuild_indexes();
    }
}

bool RepositoryEngine::has_branch(std::string_view branch) const {
    return branches_.find(branch).has_value() || refs_.read_branch(branch).has_value();
}

std::optional<BranchRef> RepositoryEngine::branch_ref(std::string_view branch) const {
    if (const auto indexed = branches_.find(branch); indexed.has_value()) {
        return BranchRef{
            indexed->name,
            indexed->branch,
            indexed->head,
            indexed->snapshot,
            indexed->epoch
        };
    }
    return refs_.read_branch(branch);
}

std::vector<BranchRef> RepositoryEngine::list_branches() const {
    const auto indexed = branches_.entries();
    if (!indexed.empty() || branches_.exists()) {
        std::vector<BranchRef> out;
        out.reserve(indexed.size());
        for (const auto& branch : indexed) {
            out.push_back(BranchRef{branch.name, branch.branch, branch.head, branch.snapshot, branch.epoch});
        }
        return out;
    }
    return refs_.list_branches();
}

std::vector<CommitRecord> RepositoryEngine::history(std::string_view branch) const {
    const auto indexed = branches_.find(branch);
    if (!indexed.has_value()) {
        throw std::out_of_range("unknown branch '" + std::string{branch} + "'");
    }
    std::vector<CommitRecord> out;
    out.reserve(indexed->history.size());
    for (const auto id : indexed->history) {
        out.push_back(commit_record(id));
    }
    return out;
}

StoredCommit RepositoryEngine::stored_commit(CommitId id) const {
    try {
        auto stored = objects_.get_canonical<StoredCommit>(id.value);
        stored.record.id = id;
        return stored;
    } catch (const std::exception& error) {
        throw std::runtime_error(
            "stored commit decode failed for " + to_hex(id.value) + ": " + error.what());
    }
}

CommitRecord RepositoryEngine::commit_record(CommitId id) const {
    auto stored = stored_commit(id);
    auto record = stored.record;
    record.id = id;
    try {
        record.events = objects_.get_canonical<std::vector<TraceEvent>>(stored.trace_object);
    } catch (const std::exception& error) {
        throw std::runtime_error(
            "trace object decode failed for commit " + to_hex(id.value)
            + " object " + to_hex(stored.trace_object) + ": " + error.what());
    }
    try {
        record.law_statuses = objects_.get_canonical<std::vector<LawStatus>>(stored.law_status_object);
    } catch (const std::exception& error) {
        throw std::runtime_error(
            "law status object decode failed for commit " + to_hex(id.value)
            + " object " + to_hex(stored.law_status_object) + ": " + error.what());
    }
    try {
        record.violations = objects_.get_canonical<std::vector<LawViolation>>(stored.violation_object);
    } catch (const std::exception& error) {
        throw std::runtime_error(
            "violation object decode failed for commit " + to_hex(id.value)
            + " object " + to_hex(stored.violation_object) + ": " + error.what());
    }
    return record;
}

WorldSnapshot RepositoryEngine::snapshot(std::string_view branch) const {
    const auto ref = branch_ref(branch);
    if (!ref.has_value()) {
        throw std::out_of_range("unknown branch '" + std::string{branch} + "'");
    }
    return snapshot(ref->head);
}

WorldSnapshot RepositoryEngine::snapshot(CommitId commit) const {
    return SnapshotMaterializer{objects_, commits_, branches_}.materialize_commit(commit);
}

MaterializedWorld RepositoryEngine::materialize_branch(std::string_view branch) const {
    const auto indexed = branches_.find(branch);
    if (!indexed.has_value()) {
        throw std::out_of_range("unknown branch '" + std::string{branch} + "'");
    }

    MaterializedWorld out;
    out.ref = BranchRef{indexed->name, indexed->branch, indexed->head, indexed->snapshot, indexed->epoch};
    out.history.reserve(indexed->history.size());
    for (const auto id : indexed->history) {
        auto stored = stored_commit(id);
        auto record = stored.record;
        record.id = id;
        record.events = objects_.get_canonical<std::vector<TraceEvent>>(stored.trace_object);
        record.law_statuses = objects_.get_canonical<std::vector<LawStatus>>(stored.law_status_object);
        record.violations = objects_.get_canonical<std::vector<LawViolation>>(stored.violation_object);
        out.history.push_back(std::move(record));
    }
    auto materializer = SnapshotMaterializer{objects_, commits_, branches_};
    out.snapshots = materializer.materialize_accepted_chain_to(indexed->head);
    const auto head_snapshot = out.snapshots.empty() ? materializer.materialize_commit(indexed->head) : out.snapshots.back().second;
    out.world = World::from_snapshot(head_snapshot);
    return out;
}

MaterializedWorld RepositoryEngine::materialize_commit(CommitId commit) const {
    const auto entry = commits_.find(commit);
    if (!entry.has_value()) {
        throw std::out_of_range("unknown commit '" + to_hex(commit.value) + "'");
    }
    MaterializedWorld out;
    if (const auto ref = branch_ref(entry->branch); ref.has_value()) {
        out.ref = *ref;
    }
    auto materializer = SnapshotMaterializer{objects_, commits_, branches_};
    out.world = World::from_snapshot(materializer.materialize_commit(commit));
    out.history.push_back(commit_record(commit));
    out.snapshots.push_back({commit, out.world.snapshot()});
    return out;
}

RepositoryIndexCheck RepositoryEngine::check_indexes() const {
    RepositoryIndexCheck report;
    const auto commit_status = commits_.check();
    const auto branch_status = branches_.check();
    const auto event_status = events_.check();
    const auto world_status = world_index_.check();
    const auto relation_status = world_index_.relations_check();
    if (!index_ok(commit_status)) {
        report.clean = false;
        report.messages.push_back(commit_status.exists ? commit_status.error : "missing commit index");
    } else {
        report.commits_checksum = commit_status.checksum;
    }
    if (!index_ok(branch_status)) {
        report.clean = false;
        report.messages.push_back(branch_status.exists ? branch_status.error : "missing branch index");
    } else {
        report.branches_checksum = branch_status.checksum;
    }
    if (!index_ok(event_status)) {
        report.clean = false;
        report.messages.push_back(event_status.exists ? event_status.error : "missing event index");
    } else {
        report.events_checksum = event_status.checksum;
    }
    if (!index_ok(world_status)) {
        report.clean = false;
        report.messages.push_back(world_status.exists ? world_status.error : "missing world index");
    } else {
        report.objects_checksum = world_status.checksum;
    }
    if (!index_ok(relation_status)) {
        report.clean = false;
        report.messages.push_back(relation_status.exists ? relation_status.error : "missing relation index");
    } else {
        report.relations_checksum = relation_status.checksum;
    }

    for (const auto& branch : branches_.entries()) {
        const auto ref = refs_.read_branch(branch.name);
        if (!ref.has_value()) {
            report.clean = false;
            report.messages.push_back("branch index references missing ref: " + branch.name);
        } else if (ref->head != branch.head || ref->snapshot != branch.snapshot || ref->epoch != branch.epoch) {
            report.clean = false;
            report.messages.push_back("branch index differs from ref: " + branch.name);
        }
        if (!contains_commit(branch.history, branch.head)) {
            report.clean = false;
            report.messages.push_back("branch index history does not contain head: " + branch.name);
        }
    }
    for (const auto& ref : refs_.list_branches()) {
        if (!branches_.find(ref.name).has_value()) {
            report.clean = false;
            report.messages.push_back("branch ref missing from branch index: " + ref.name);
        }
    }
    return report;
}

RepositoryBackendStats RepositoryEngine::stats() const {
    RepositoryBackendStats stats;
    stats.objects = count_content_objects(root_);
    const auto commit_stats = commits_.stats();
    stats.commits = commit_stats.commits;
    stats.snapshots = commit_stats.snapshots;
    stats.program_objects = commit_stats.program_objects;
    stats.delta_objects = commit_stats.delta_objects;
    stats.branches = branches_.entries().size();
    stats.index_status = check_indexes().clean ? "clean" : "dirty";
    stats.wal_status = wal_.recover().incomplete_commit ? "incomplete" : "clean";
    return stats;
}

void RepositoryEngine::rebuild_indexes() const {
    std::vector<CommitIndexEntry> commit_entries;
    std::vector<BranchIndexEntry> branch_entries;
    std::vector<EventNameIndexEntry> event_entries;
    std::vector<ObjectTouchIndexEntry> touch_entries;
    EventIndexStore rebuilt_events{root_};
    WorldIndexStore rebuilt_world{root_};
    rebuilt_events.remove();
    rebuilt_world.remove();

    std::set<std::string> seen_commits;
    for (const auto& ref : refs_.list_branches()) {
        auto ids = read_history_ids(ref.name);
        const auto accepted = latest_accepted(ids);
        if (ids.empty() || !accepted.has_value() || *accepted != ref.head) {
            ids = parent_chain(ref.head);
        }
        if (!contains_commit(ids, ref.head)) {
            ids.push_back(ref.head);
        }

        for (const auto id : ids) {
            auto stored = stored_commit(id);
            stored.record.id = id;
            if (seen_commits.insert(commit_key(id)).second) {
                commit_entries.push_back(CommitIndexEntry{
                    id,
                    id.value,
                    ref.name,
                    stored.record.parents,
                    stored.before_snapshot_object,
                    stored.after_snapshot_object,
                    stored.delta_object,
                    stored.program_object,
                    stored.record.before_root,
                    stored.record.after_root,
                    stored.record.checkpoint_snapshot_object,
                    stored.record.checkpoint_distance,
                    stored.record.graph_page_roots,
                    stored.record.before_epoch,
                    stored.record.after_epoch,
                    stored.record.accepted,
                    stored.record.origin
                });
            }

            auto record = stored.record;
            record.id = id;
            record.events = objects_.get_canonical<std::vector<TraceEvent>>(stored.trace_object);
            record.law_statuses = objects_.get_canonical<std::vector<LawStatus>>(stored.law_status_object);
            record.violations = objects_.get_canonical<std::vector<LawViolation>>(stored.violation_object);
            const auto after = SnapshotMaterializer{objects_, commits_, branches_}.materialize_commit(id);
            rebuilt_events.index_commit(ref.name, record, after);
            rebuilt_world.index_commit(id, record.after_root, after);
        }

        branch_entries.push_back(BranchIndexEntry{ref.name, ref.branch, ref.head, ref.snapshot, ref.epoch, ids});
        const auto head_snapshot = snapshot(ref.head);
        rebuilt_world.update_branch(ref.name, ref.snapshot, head_snapshot);
    }

    commits_.write(std::move(commit_entries));
    branches_.write(std::move(branch_entries));
}

void RepositoryEngine::compact() const {
    rebuild_indexes();
    (void)PackedContentStore{root_}.compact_loose_objects();
    wal_.truncate();
    std::error_code ignored;
    if (std::filesystem::exists(root_ / "staging")) {
        std::filesystem::remove_all(root_ / "staging", ignored);
    }
    for (const auto& dir : {root_ / "refs", root_ / "history", root_ / "index"}) {
        if (!std::filesystem::exists(dir)) {
            continue;
        }
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".tmp") {
                std::filesystem::remove(entry.path(), ignored);
            }
        }
    }
}

CommitIndex& RepositoryEngine::commits() noexcept {
    return commits_;
}

BranchIndex& RepositoryEngine::branches() noexcept {
    return branches_;
}

WorldIndexStore& RepositoryEngine::world_index() noexcept {
    return world_index_;
}

EventIndexStore& RepositoryEngine::events() noexcept {
    return events_;
}

const CommitIndex& RepositoryEngine::commits() const noexcept {
    return commits_;
}

const BranchIndex& RepositoryEngine::branches() const noexcept {
    return branches_;
}

const WorldIndexStore& RepositoryEngine::world_index() const noexcept {
    return world_index_;
}

const EventIndexStore& RepositoryEngine::events() const noexcept {
    return events_;
}

std::vector<CommitId> RepositoryEngine::read_history_ids(std::string_view branch) const {
    std::vector<CommitId> out;
    std::ifstream input(history_path(root_, branch));
    if (!input) {
        return out;
    }
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        const auto id = parse_hash256(line);
        if (!id.has_value()) {
            throw std::runtime_error("invalid commit id in branch history");
        }
        out.push_back(CommitId{*id});
    }
    return out;
}

std::vector<CommitId> RepositoryEngine::parent_chain(CommitId head) const {
    std::vector<CommitId> reversed;
    auto current = std::optional<CommitId>{head};
    while (current.has_value()) {
        reversed.push_back(*current);
        auto stored = stored_commit(*current);
        if (stored.record.parents.empty()) {
            current = std::nullopt;
        } else {
            current = stored.record.parents.front();
        }
    }
    std::ranges::reverse(reversed);
    return reversed;
}

std::optional<CommitId> RepositoryEngine::latest_accepted(const std::vector<CommitId>& ids) const {
    std::optional<CommitId> latest;
    for (const auto id : ids) {
        try {
            if (stored_commit(id).record.accepted) {
                latest = id;
            }
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    return latest;
}

}  // namespace pv
