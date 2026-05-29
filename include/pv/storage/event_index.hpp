// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "pv/core/id.hpp"
#include "pv/core/snapshot.hpp"
#include "pv/runtime/commit_record.hpp"
#include "pv/runtime/ids.hpp"
#include "pv/storage/index_store.hpp"

namespace pv {

struct EventNameIndexEntry {
    std::string branch;
    std::string event;
    std::vector<CommitId> commits;
};

struct ObjectTouchIndexEntry {
    std::string branch;
    ObjectId object;
    std::vector<CommitId> commits;
};

struct EventIndexStats {
    std::size_t event_names{0};
    std::size_t object_touches{0};
};

class EventIndexStore {
public:
    explicit EventIndexStore(std::filesystem::path root);

    [[nodiscard]] bool exists() const;
    [[nodiscard]] std::vector<EventNameIndexEntry> event_entries() const;
    [[nodiscard]] std::vector<ObjectTouchIndexEntry> touch_entries() const;
    [[nodiscard]] std::vector<CommitId> commits_for_event(std::string_view branch, std::string_view event) const;
    [[nodiscard]] std::vector<CommitId> commits_touching_object(std::string_view branch, ObjectId object) const;
    [[nodiscard]] EventIndexStats stats() const;
    [[nodiscard]] IndexFileStatus check() const;
    [[nodiscard]] Hash256 checksum() const;

    void write(std::vector<EventNameIndexEntry> events, std::vector<ObjectTouchIndexEntry> touches) const;
    void index_commit(std::string_view branch, const CommitRecord& record, const WorldSnapshot& after) const;
    void remove() const;

private:
    IndexStore store_;
};

}  // namespace pv
