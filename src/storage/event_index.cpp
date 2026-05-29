// SPDX-License-Identifier: Apache-2.0
#include "pv/storage/event_index.hpp"

#include <algorithm>
#include <set>
#include <tuple>
#include <utility>

#include "pv/hash/canonical.hpp"

namespace pv {
namespace {

void write_commit_id(IndexPayloadWriter& writer, CommitId id) {
    writer.hash(id.value);
}

CommitId read_commit_id(IndexPayloadReader& reader) {
    return CommitId{reader.hash()};
}

void write_commit_ids(IndexPayloadWriter& writer, const std::vector<CommitId>& commits) {
    writer.u64(commits.size());
    for (const auto commit : commits) {
        write_commit_id(writer, commit);
    }
}

std::vector<CommitId> read_commit_ids(IndexPayloadReader& reader) {
    const auto size = reader.u64();
    std::vector<CommitId> commits;
    commits.reserve(static_cast<std::size_t>(size));
    for (std::uint64_t index = 0; index < size; ++index) {
        commits.push_back(read_commit_id(reader));
    }
    return commits;
}

void write_object(IndexPayloadWriter& writer, ObjectId object) {
    writer.u32(object.index);
    writer.u32(object.generation);
}

ObjectId read_object(IndexPayloadReader& reader) {
    return ObjectId{reader.u32(), reader.u32()};
}

std::string commit_key(CommitId id) {
    return to_hex(id.value);
}

void sort_unique_commits(std::vector<CommitId>& commits) {
    std::ranges::sort(commits, [](CommitId left, CommitId right) {
        return commit_key(left) < commit_key(right);
    });
    commits.erase(std::ranges::unique(commits).begin(), commits.end());
}

bool event_touches_object(const TraceEvent& event, const ObjectSnapshot& object) {
    const auto id = to_string(object.id);
    for (const auto& key : {"object", "from", "to", "id"}) {
        const auto iter = event.fields.find(key);
        if (iter != event.fields.end() && (iter->second == object.name || iter->second == id)) {
            return true;
        }
    }
    return false;
}

void sort_events(std::vector<EventNameIndexEntry>& events) {
    for (auto& event : events) {
        sort_unique_commits(event.commits);
    }
    std::ranges::sort(events, [](const EventNameIndexEntry& left, const EventNameIndexEntry& right) {
        return std::tie(left.branch, left.event) < std::tie(right.branch, right.event);
    });
}

void sort_touches(std::vector<ObjectTouchIndexEntry>& touches) {
    for (auto& touch : touches) {
        sort_unique_commits(touch.commits);
    }
    std::ranges::sort(touches, [](const ObjectTouchIndexEntry& left, const ObjectTouchIndexEntry& right) {
        return std::tie(left.branch, left.object.index, left.object.generation)
            < std::tie(right.branch, right.object.index, right.object.generation);
    });
}

}  // namespace

EventIndexStore::EventIndexStore(std::filesystem::path root)
    : store_(std::move(root), "events.idx", "PVEVENTIDX1") {}

bool EventIndexStore::exists() const {
    return store_.exists();
}

std::vector<EventNameIndexEntry> EventIndexStore::event_entries() const {
    if (!exists()) {
        return {};
    }
    const auto payload = store_.read_payload();
    IndexPayloadReader reader{payload};
    const auto size = reader.u64();
    std::vector<EventNameIndexEntry> events;
    events.reserve(static_cast<std::size_t>(size));
    for (std::uint64_t index = 0; index < size; ++index) {
        events.push_back(EventNameIndexEntry{reader.string(), reader.string(), read_commit_ids(reader)});
    }
    const auto touch_count = reader.u64();
    for (std::uint64_t index = 0; index < touch_count; ++index) {
        (void)reader.string();
        (void)read_object(reader);
        (void)read_commit_ids(reader);
    }
    reader.expect_end();
    return events;
}

std::vector<ObjectTouchIndexEntry> EventIndexStore::touch_entries() const {
    if (!exists()) {
        return {};
    }
    const auto payload = store_.read_payload();
    IndexPayloadReader reader{payload};
    const auto event_count = reader.u64();
    for (std::uint64_t index = 0; index < event_count; ++index) {
        (void)reader.string();
        (void)reader.string();
        (void)read_commit_ids(reader);
    }
    const auto touch_count = reader.u64();
    std::vector<ObjectTouchIndexEntry> touches;
    touches.reserve(static_cast<std::size_t>(touch_count));
    for (std::uint64_t index = 0; index < touch_count; ++index) {
        touches.push_back(ObjectTouchIndexEntry{reader.string(), read_object(reader), read_commit_ids(reader)});
    }
    reader.expect_end();
    return touches;
}

std::vector<CommitId> EventIndexStore::commits_for_event(std::string_view branch, std::string_view event) const {
    for (const auto& entry : event_entries()) {
        if (entry.branch == branch && entry.event == event) {
            return entry.commits;
        }
    }
    return {};
}

std::vector<CommitId> EventIndexStore::commits_touching_object(std::string_view branch, ObjectId object) const {
    for (const auto& entry : touch_entries()) {
        if (entry.branch == branch && entry.object == object) {
            return entry.commits;
        }
    }
    return {};
}

EventIndexStats EventIndexStore::stats() const {
    return EventIndexStats{event_entries().size(), touch_entries().size()};
}

IndexFileStatus EventIndexStore::check() const {
    return store_.check();
}

Hash256 EventIndexStore::checksum() const {
    return store_.checksum();
}

void EventIndexStore::write(std::vector<EventNameIndexEntry> events, std::vector<ObjectTouchIndexEntry> touches) const {
    sort_events(events);
    events.erase(std::ranges::unique(events, [](const auto& left, const auto& right) {
        return left.branch == right.branch && left.event == right.event;
    }).begin(), events.end());
    sort_touches(touches);
    touches.erase(std::ranges::unique(touches, [](const auto& left, const auto& right) {
        return left.branch == right.branch && left.object == right.object;
    }).begin(), touches.end());

    IndexPayloadWriter writer;
    writer.u64(events.size());
    for (const auto& event : events) {
        writer.string(event.branch);
        writer.string(event.event);
        write_commit_ids(writer, event.commits);
    }
    writer.u64(touches.size());
    for (const auto& touch : touches) {
        writer.string(touch.branch);
        write_object(writer, touch.object);
        write_commit_ids(writer, touch.commits);
    }
    store_.write_payload(writer.bytes());
}

void EventIndexStore::index_commit(std::string_view branch, const CommitRecord& record, const WorldSnapshot& after) const {
    auto events = event_entries();
    auto touches = touch_entries();
    std::set<std::string> event_names_seen;
    std::set<std::pair<ObjectIndex, Generation>> objects_seen;

    for (const auto& event : record.events) {
        if (event_names_seen.insert(event.event).second) {
            auto iter = std::ranges::find_if(events, [&](const auto& entry) {
                return entry.branch == branch && entry.event == event.event;
            });
            if (iter == events.end()) {
                events.push_back(EventNameIndexEntry{std::string{branch}, event.event, {record.id}});
            } else {
                iter->commits.push_back(record.id);
            }
        }

        for (const auto& object : after.objects) {
            if (!event_touches_object(event, object)) {
                continue;
            }
            const auto object_key = std::pair{object.id.index, object.id.generation};
            if (!objects_seen.insert(object_key).second) {
                continue;
            }
            auto iter = std::ranges::find_if(touches, [&](const auto& entry) {
                return entry.branch == branch && entry.object == object.id;
            });
            if (iter == touches.end()) {
                touches.push_back(ObjectTouchIndexEntry{std::string{branch}, object.id, {record.id}});
            } else {
                iter->commits.push_back(record.id);
            }
        }
    }

    write(std::move(events), std::move(touches));
}

void EventIndexStore::remove() const {
    store_.remove();
}

}  // namespace pv
