// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "pv/trace/event.hpp"

namespace pv {

class TraceRecorder {
public:
    void append(TraceEvent event);
    void append(std::vector<TraceEvent> events);

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] const std::vector<TraceEvent>& events() const noexcept;
    [[nodiscard]] std::string to_jsonl() const;

private:
    std::vector<TraceEvent> events_;
};

}  // namespace pv
