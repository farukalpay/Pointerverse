// SPDX-License-Identifier: Apache-2.0
#include "pv/storage/repository_gc.hpp"

#include <filesystem>
#include <stdexcept>
#include <utility>

#include "pv/storage/object_codec.hpp"

namespace pv {
namespace {

void add_hash(std::set<std::string>& out, Hash256 hash) {
    if (!empty(hash)) {
        out.insert(to_hex(hash));
    }
}

}  // namespace

RepositoryGc::RepositoryGc(std::filesystem::path root, RepositoryEngine& engine, ContentStore& objects)
    : root_(std::move(root)), engine_(engine), objects_(objects) {}

std::set<std::string> RepositoryGc::reachable() const {
    std::set<std::string> out;
    for (const auto& branch : engine_.branches().entries()) {
        add_hash(out, branch.head.value);
        for (const auto commit : branch.history) {
            add_hash(out, commit.value);
            auto stored = objects_.get_canonical<StoredCommit>(commit.value);
            stored.record.id = commit;
            add_hash(out, stored.delta_object);
            add_hash(out, stored.program_object);
            add_hash(out, stored.trace_object);
            add_hash(out, stored.law_status_object);
            add_hash(out, stored.violation_object);
            add_hash(out, stored.morphism_path_object);
            if (stored.format_version < 4) {
                add_hash(out, stored.before_snapshot_object);
                add_hash(out, stored.after_snapshot_object);
            }
            add_hash(out, stored.record.checkpoint_snapshot_object);
            for (const auto root : stored.record.graph_page_roots) {
                add_hash(out, root);
            }
        }
    }
    return out;
}

std::set<std::string> RepositoryGc::loose_objects() const {
    std::set<std::string> out;
    const auto object_root = root_ / "objects";
    if (!std::filesystem::exists(object_root)) {
        return out;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(object_root)) {
        if (!entry.is_regular_file() || entry.path().extension() == ".tmp") {
            continue;
        }
        const auto hash = parse_hash256(entry.path().filename().string());
        if (hash.has_value()) {
            out.insert(to_hex(*hash));
        }
    }
    return out;
}

ReachabilityReport RepositoryGc::mark() const {
    const auto marked = reachable();
    const auto loose = loose_objects();
    ReachabilityReport report;
    report.reachable_objects = marked.size();
    for (const auto& object : loose) {
        if (!marked.contains(object)) {
            report.unreachable_objects += 1;
        }
    }
    return report;
}

std::filesystem::path RepositoryGc::quarantine_path(Hash256 id) const {
    const auto hex = to_hex(id);
    return root_ / "quarantine" / "objects" / hex.substr(0, 2) / hex;
}

ReachabilityReport RepositoryGc::quarantine_unreachable() const {
    auto report = mark();
    const auto marked = reachable();
    for (const auto& object : loose_objects()) {
        if (marked.contains(object)) {
            continue;
        }
        const auto hash = parse_hash256(object);
        if (!hash.has_value()) {
            continue;
        }
        const auto source = objects_.object_path(*hash);
        const auto target = quarantine_path(*hash);
        std::filesystem::create_directories(target.parent_path());
        if (std::filesystem::exists(source)) {
            std::filesystem::rename(source, target);
            report.quarantined_objects += 1;
        }
    }
    return report;
}

void RepositoryGc::prune_quarantine() const {
    std::error_code ignored;
    std::filesystem::remove_all(root_ / "quarantine", ignored);
}

}  // namespace pv
