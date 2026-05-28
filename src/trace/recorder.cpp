// SPDX-License-Identifier: Apache-2.0
#include "pv/trace/recorder.hpp"

#include <nlohmann/json.hpp>

namespace pv {

void TraceRecorder::append(TraceEvent event) {
    events_.push_back(std::move(event));
}

void TraceRecorder::append(std::vector<TraceEvent> events) {
    for (auto& event : events) {
        append(std::move(event));
    }
}

bool TraceRecorder::empty() const noexcept {
    return events_.empty();
}

std::size_t TraceRecorder::size() const noexcept {
    return events_.size();
}

const std::vector<TraceEvent>& TraceRecorder::events() const noexcept {
    return events_;
}

std::string TraceRecorder::to_jsonl() const {
    std::string out;
    for (const auto& event : events_) {
        nlohmann::json json{
            {"epoch", event.epoch.value},
            {"event", event.event},
            {"fields", event.fields},
            {"measurements", event.measurements}
        };
        out += json.dump();
        out += '\n';
    }
    return out;
}

}  // namespace pv
