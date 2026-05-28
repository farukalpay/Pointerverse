// SPDX-License-Identifier: Apache-2.0
#include "pv/storage/repository.hpp"

#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "pv/hash/hasher.hpp"
#include "pv/sentinel/boot_gate.hpp"

namespace pv {
namespace {

std::vector<std::byte> text_payload(std::string_view value) {
    std::vector<std::byte> out;
    out.reserve(value.size());
    for (const auto ch : value) {
        out.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }
    return out;
}

std::string first_world_name(std::string_view jsonl, std::string fallback) {
    std::istringstream input{std::string{jsonl}};
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        try {
            const auto json = nlohmann::json::parse(line);
            const auto fields = json.value("fields", nlohmann::json::object());
            if (!fields.is_object()) {
                continue;
            }
            const auto iter = fields.find("world");
            if (iter != fields.end() && iter->is_string() && !iter->get<std::string>().empty()) {
                return iter->get<std::string>();
            }
        } catch (const std::exception&) {
            return fallback;
        }
    }
    return fallback;
}

struct ReplayGroup {
    Epoch epoch;
    Delta delta;
    std::size_t first_line{0};
    std::size_t state_events{0};
    std::size_t empty_markers{0};
    std::uint32_t next_temp{1};
    std::unordered_map<std::string, TempObjectId> created_objects;
};

bool is_state_event(std::string_view event) {
    return event == "object.create"
        || event == "object.update"
        || event == "pointer.create"
        || event == "pointer.remove";
}

bool is_empty_transition_marker(std::string_view event) {
    return event == "world.evolve"
        || event == "evolution.step"
        || event == "morphism.apply"
        || event == "morphism.compose";
}

bool is_metadata_event(std::string_view event) {
    return event == "law.check"
        || event == "world.transition.rejected"
        || event == "morphism.compose.rejected"
        || event == "branch.fork";
}

std::string field(const nlohmann::json& fields, std::string_view name, std::string fallback = {}) {
    if (!fields.is_object()) {
        return fallback;
    }
    const auto iter = fields.find(std::string{name});
    if (iter == fields.end() || !iter->is_string()) {
        return fallback;
    }
    return iter->get<std::string>();
}

double measurement(const nlohmann::json& measurements, std::string_view name, double fallback) {
    if (!measurements.is_object()) {
        return fallback;
    }
    const auto iter = measurements.find(std::string{name});
    if (iter == measurements.end() || !iter->is_number()) {
        return fallback;
    }
    return iter->get<double>();
}

PointerId pointer_id_from_string(const std::string& value) {
    if (value.size() < 2 || value.front() != 'P') {
        throw std::invalid_argument(fmt::format("invalid pointer id '{}'", value));
    }
    return PointerId{std::stoull(value.substr(1))};
}

ObjectRef object_ref_for(World& world, ReplayGroup& group, const std::string& name) {
    if (const auto iter = group.created_objects.find(name); iter != group.created_objects.end()) {
        return ObjectRef{iter->second};
    }
    return ObjectRef{world.object_by_name(name)};
}

std::optional<ReplayError> append_state_event(
    World& world,
    ReplayGroup& group,
    const std::string& event,
    const nlohmann::json& fields,
    const nlohmann::json& measurements,
    std::size_t line) {
    try {
        if (event == "object.create") {
            const auto object_name = field(fields, "object");
            const auto type_name = field(fields, "type");
            if (object_name.empty() || type_name.empty()) {
                return ReplayError{line, event, "object.create requires object and type fields"};
            }
            const auto temp = TempObjectId{group.next_temp++};
            group.created_objects.emplace(object_name, temp);
            group.delta.append_create(ObjectCreate{
                temp,
                object_name,
                world.type_id(type_name),
                existence_state_from_string(field(fields, "existence", "Alive")),
                {}
            });
            group.state_events += 1;
            return std::nullopt;
        }

        if (event == "object.update") {
            const auto object_name = field(fields, "object");
            if (object_name.empty()) {
                return ReplayError{line, event, "object.update requires object field"};
            }
            std::optional<TypeId> type;
            if (const auto type_name = field(fields, "type"); !type_name.empty()) {
                type = world.type_id(type_name);
            }
            std::optional<ExistenceState> existence;
            if (const auto existence_name = field(fields, "existence"); !existence_name.empty()) {
                existence = existence_state_from_string(existence_name);
            }
            group.delta.append_update(ObjectUpdate{object_ref_for(world, group, object_name), type, existence});
            group.state_events += 1;
            return std::nullopt;
        }

        if (event == "pointer.create") {
            const auto from = field(fields, "from");
            const auto to = field(fields, "to");
            const auto relation = field(fields, "relation");
            if (from.empty() || to.empty() || relation.empty()) {
                return ReplayError{line, event, "pointer.create requires from, to, and relation fields"};
            }
            group.delta.append_link(PointerCreate{
                object_ref_for(world, group, from),
                object_ref_for(world, group, to),
                world.relation_type(relation),
                causal_role_from_string(field(fields, "role", "Structural")),
                Weight{measurement(measurements, "weight", 1.0)},
                field(fields, "law_domain", "core"),
                {}
            });
            group.state_events += 1;
            return std::nullopt;
        }

        if (event == "pointer.remove") {
            group.delta.append_unlink(PointerRemove{pointer_id_from_string(field(fields, "pointer"))});
            group.state_events += 1;
            return std::nullopt;
        }
    } catch (const std::exception& error) {
        return ReplayError{line, event, error.what()};
    }

    return ReplayError{line, event, "unsupported state event"};
}

void maybe_reset_world(World& world, const nlohmann::json& fields) {
    const auto world_name = field(fields, "world");
    if (!world_name.empty() && world.epoch().value == 0 && world.objects().empty() && world.name() != world_name) {
        world.reset(world_name);
    }
}

void flush_group(
    std::optional<ReplayGroup>& group,
    RuntimeReplayResult& result,
    Repository& repo,
    std::string_view branch,
    const Verifier& verifier) {
    if (!group.has_value()) {
        return;
    }

    const auto replayed_events = group->state_events == 0 && group->empty_markers > 0 ? 1 : group->state_events;
    if (replayed_events == 0) {
        group.reset();
        return;
    }

    Transaction tx;
    tx.origin = TransactionOrigin::Replay;
    tx.label = fmt::format("replay epoch {}", group->epoch.value);
    tx.delta = group->delta;
    tx.allow_empty = group->empty_markers > 0;
    if (group->epoch.value > 0) {
        tx.expected_base_epoch = Epoch{group->epoch.value - 1};
    }

    const auto record = repo.commit(branch, tx, verifier);
    if (!record.has_value() || !record->accepted) {
        result.errors.push_back(ReplayError{
            group->first_line,
            "epoch",
            fmt::format("repository replay commit for epoch {} was rejected", group->epoch.value)
        });
    } else {
        result.events_replayed += replayed_events;
        result.commits_replayed += 1;
    }
    group.reset();
}

}  // namespace

Repository::Repository(std::filesystem::path root)
    : root_(std::move(root)),
      manifest_(root_),
      objects_(root_),
      refs_(root_),
      wal_(root_) {}

Repository Repository::init(std::filesystem::path root) {
    std::filesystem::create_directories(root);
    std::filesystem::create_directories(root / "objects");
    std::filesystem::create_directories(root / "refs" / "branches");
    std::filesystem::create_directories(root / "history" / "branches");
    std::filesystem::create_directories(root / "wal");
    std::filesystem::create_directories(root / "locks");

    Repository repo{std::move(root)};
    repo.manifest_.write(RepositoryManifest{});
    repo.refs_.set_current_branch("main");
    repo.wal_.truncate();
    return repo;
}

Repository Repository::open(std::filesystem::path root) {
    Repository repo{std::move(root)};
    if (!repo.manifest_.exists()) {
        throw std::runtime_error("not a Pointerverse repository");
    }
    (void)repo.wal_.recover();
    repo.load();
    return repo;
}

Repository Repository::open_with_sentinel(std::filesystem::path root, BootGateResult* result) {
    auto boot = run_boot_gate(root);
    if (result != nullptr) {
        *result = boot;
    }
    if (!boot.ok) {
        throw std::runtime_error("sentinel boot failed at " + to_string(boot.failed_at));
    }
    return Repository::open(std::move(root));
}

const std::filesystem::path& Repository::root() const noexcept {
    return root_;
}

RepositoryStatus Repository::status() const {
    return RepositoryStatus{root_, refs_.current_branch(), refs_.list_branches().size()};
}

BranchId Repository::create_branch(std::string name, World initial) {
    const auto id = store_.create_branch(name, std::move(initial));
    const auto history = store_.history(id);
    persist_record(store_.branch(id).name, history.back(), Delta{}, std::nullopt, {});
    refs_.update_branch(branch_ref_from_runtime(store_.branch(id).name));
    write_history(store_.branch(id).name, history);
    return id;
}

ForkResult Repository::fork(std::string_view source, std::string new_name) {
    const auto source_id = require_branch(source);
    const auto result = store_.fork(source_id, std::move(new_name));
    const auto& forked = store_.branch(result.forked);
    refs_.update_branch(branch_ref_from_runtime(forked.name));
    write_history(forked.name, store_.history(result.forked));
    return result;
}

std::optional<CommitRecord> Repository::commit(std::string_view branch, Transaction tx, const Verifier& verifier) {
    const auto branch_id = require_branch(branch);
    const auto record = store_.commit(branch_id, tx, verifier);
    if (!record.has_value()) {
        return std::nullopt;
    }

    persist_record(branch, *record, tx.delta, tx.program, tx.morphism_path);
    if (record->accepted) {
        refs_.update_branch(branch_ref_from_runtime(branch));
    }
    write_history(branch, store_.history(branch_id));
    return record;
}

RuntimeReplayResult Repository::replay_trace(std::string_view branch_name, std::string_view jsonl, const Verifier& verifier) {
    std::string branch{branch_name};
    if (branch.empty()) {
        branch = refs_.current_branch();
    }
    if (!store_.find_branch(branch).has_value()) {
        (void)create_branch(branch, World{first_world_name(jsonl, "world")});
    }

    RuntimeReplayResult result;
    result.branch = require_branch(branch);
    result.branch_name = branch;

    std::istringstream input{std::string{jsonl}};
    std::string line;
    std::optional<ReplayGroup> group;
    std::size_t line_number = 0;

    while (std::getline(input, line)) {
        line_number += 1;
        if (line.empty()) {
            continue;
        }

        result.events_read += 1;

        nlohmann::json json;
        try {
            json = nlohmann::json::parse(line);
        } catch (const std::exception& error) {
            result.errors.push_back(ReplayError{line_number, {}, error.what()});
            continue;
        }

        const auto event = json.value("event", std::string{});
        const auto epoch = Epoch{json.value("epoch", std::uint64_t{0})};
        const auto fields = json.value("fields", nlohmann::json::object());
        const auto measurements = json.value("measurements", nlohmann::json::object());
        maybe_reset_world(store_.mutable_world(result.branch), fields);

        if (is_state_event(event) || is_empty_transition_marker(event)) {
            if (group.has_value() && group->epoch != epoch) {
                flush_group(group, result, *this, branch, verifier);
            }
            if (!group.has_value()) {
                ReplayGroup next_group;
                next_group.epoch = epoch;
                next_group.first_line = line_number;
                group = std::move(next_group);
            }

            if (is_empty_transition_marker(event)) {
                group->empty_markers += 1;
                continue;
            }

            if (auto error = append_state_event(
                    store_.mutable_world(result.branch),
                    *group,
                    event,
                    fields,
                    measurements,
                    line_number);
                error.has_value()) {
                result.errors.push_back(std::move(*error));
            }
            continue;
        }

        if (group.has_value() && group->epoch != epoch) {
            flush_group(group, result, *this, branch, verifier);
        }

        if (is_metadata_event(event)) {
            result.metadata_events += 1;
            continue;
        }

        result.errors.push_back(ReplayError{line_number, event, "unsupported event"});
    }

    flush_group(group, result, *this, branch, verifier);
    result.final_hash = store_.world(result.branch).canonical_hash();
    return result;
}

MergeAnalysis Repository::analyze_merge(std::string_view left, std::string_view right) const {
    return store_.analyze_merge(require_branch(left), require_branch(right));
}

std::vector<BranchRef> Repository::list_branches() const {
    return refs_.list_branches();
}

bool Repository::has_branch(std::string_view name) const {
    return store_.find_branch(name).has_value();
}

std::vector<CommitRecord> Repository::history(std::string_view branch) const {
    return store_.history(require_branch(branch));
}

const World& Repository::world(std::string_view branch) const {
    return store_.world(require_branch(branch));
}

World& Repository::mutable_world(std::string_view branch) {
    return store_.mutable_world(require_branch(branch));
}

void Repository::checkout(std::string_view branch) {
    (void)require_branch(branch);
    refs_.set_current_branch(branch);
    auto manifest = manifest_.read();
    manifest.current_branch = std::string{branch};
    manifest_.write(manifest);
}

std::string Repository::current_branch() const {
    return refs_.current_branch();
}

ContentStore& Repository::objects() noexcept {
    return objects_;
}

const ContentStore& Repository::objects() const noexcept {
    return objects_;
}

const RefStore& Repository::refs() const noexcept {
    return refs_;
}

const WorldStore& Repository::runtime() const noexcept {
    return store_;
}

void Repository::load() {
    for (const auto& ref : refs_.list_branches()) {
        std::vector<CommitRecord> history;
        std::vector<std::pair<CommitId, WorldSnapshot>> snapshots;
        for (const auto& id : read_history_ids(ref.name)) {
            auto stored = objects_.get_canonical<StoredCommit>(id.value);
            stored.record.id = id;
            stored.record.events = objects_.get_canonical<std::vector<TraceEvent>>(stored.trace_object);
            stored.record.law_statuses = objects_.get_canonical<std::vector<LawStatus>>(stored.law_status_object);
            stored.record.violations = objects_.get_canonical<std::vector<LawViolation>>(stored.violation_object);
            if (stored.record.accepted) {
                snapshots.push_back({id, objects_.get_canonical<WorldSnapshot>(stored.after_snapshot_object)});
            }
            history.push_back(std::move(stored.record));
        }

        if (history.empty()) {
            auto stored = objects_.get_canonical<StoredCommit>(ref.head.value);
            stored.record.id = ref.head;
            history.push_back(std::move(stored.record));
            snapshots.push_back({ref.head, objects_.get_canonical<WorldSnapshot>(ref.snapshot)});
        }

        auto head_snapshot = objects_.get_canonical<WorldSnapshot>(ref.snapshot);
        (void)store_.restore_branch(
            ref.branch,
            ref.name,
            World::from_snapshot(head_snapshot),
            std::move(history),
            std::move(snapshots));
    }
}

void Repository::persist_record(
    std::string_view branch,
    const CommitRecord& record,
    const Delta& delta,
    const std::optional<Program>& program,
    const std::vector<std::string>& morphism_path) {
    const auto branch_id = require_branch(branch);
    const auto& snapshot_store = store_.snapshots();
    const auto before_snapshot = snapshot_store.get(record.before_snapshot);
    const auto after_snapshot = snapshot_store.get(record.after_snapshot);

    auto stored = make_stored_commit(record);
    wal_.append(WalOp::BeginCommit, text_payload(record.label));
    stored.before_snapshot_object = objects_.put_canonical(before_snapshot);
    wal_.append(WalOp::PutObject, stored.before_snapshot_object.value);
    stored.after_snapshot_object = objects_.put_canonical(after_snapshot);
    wal_.append(WalOp::PutObject, stored.after_snapshot_object.value);
    stored.delta_object = objects_.put_canonical(delta);
    wal_.append(WalOp::PutObject, stored.delta_object.value);
    if (program.has_value()) {
        stored.program_object = objects_.put_canonical(*program);
        if (stored.program_object != record.program_hash) {
            throw std::runtime_error("stored program hash does not match commit record");
        }
        wal_.append(WalOp::PutObject, stored.program_object.value);
    }
    stored.trace_object = objects_.put_canonical(record.events);
    wal_.append(WalOp::PutObject, stored.trace_object.value);
    stored.law_status_object = objects_.put_canonical(record.law_statuses);
    wal_.append(WalOp::PutObject, stored.law_status_object.value);
    stored.violation_object = objects_.put_canonical(record.violations);
    wal_.append(WalOp::PutObject, stored.violation_object.value);
    stored.morphism_path_object = objects_.put_bytes(canonical_encode_morphism_path(morphism_path));
    wal_.append(WalOp::PutObject, stored.morphism_path_object.value);

    const auto commit_object = objects_.put_canonical(stored);
    if (commit_object != record.id.value) {
        throw std::runtime_error("stored commit hash does not match commit id");
    }

    if (record.accepted) {
        wal_.append(WalOp::BindSnapshot, stored.after_snapshot_object.value);
        wal_.append(WalOp::AddCommitNode, record.id.value.value);
        refs_.update_branch(branch_ref_from_runtime(branch));
        wal_.append(WalOp::UpdateBranchRef, text_payload(std::string{branch}));
    }
    wal_.append(WalOp::EndCommit, record.id.value.value);

    (void)branch_id;
}

std::filesystem::path Repository::history_path(std::string_view branch) const {
    if (!RefStore::valid_branch_name(branch)) {
        throw std::invalid_argument("invalid branch name");
    }
    return root_ / "history" / "branches" / std::filesystem::path{std::string{branch}};
}

void Repository::write_history(std::string_view branch, const std::vector<CommitRecord>& history) const {
    const auto path = history_path(branch);
    std::filesystem::create_directories(path.parent_path());
    const auto tmp = path.string() + ".tmp";
    std::ofstream output(tmp, std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot write branch history");
    }
    for (const auto& record : history) {
        output << to_hex(record.id.value) << '\n';
    }
    output.close();
    std::filesystem::rename(tmp, path);
}

std::vector<CommitId> Repository::read_history_ids(std::string_view branch) const {
    std::vector<CommitId> out;
    std::ifstream input(history_path(branch));
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

BranchId Repository::require_branch(std::string_view name) const {
    const auto id = store_.find_branch(name);
    if (!id.has_value()) {
        throw std::out_of_range(fmt::format("unknown branch '{}'", name));
    }
    return *id;
}

BranchRef Repository::branch_ref_from_runtime(std::string_view branch_name) const {
    const auto id = require_branch(branch_name);
    const auto& branch = store_.branch(id);
    if (!branch.head.has_value()) {
        throw std::runtime_error("branch has no head commit");
    }

    BranchRef ref;
    ref.name = branch.name;
    ref.branch = branch.id;
    ref.head = *branch.head;
    ref.snapshot = store_.world(id).canonical_hash();
    ref.epoch = branch.epoch;
    return ref;
}

}  // namespace pv
