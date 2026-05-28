// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "pv/core/world.hpp"
#include "pv/law/verifier.hpp"

namespace pv {

struct ReplayError {
    std::size_t line{0};
    std::string event;
    std::string message;
};

struct ReplayResult {
    World world;
    std::size_t events_read{0};
    std::size_t events_replayed{0};
    std::size_t metadata_events{0};
    std::vector<ReplayError> errors;
    std::uint64_t final_hash{0};
};

class TraceReplayer {
public:
    [[nodiscard]] ReplayResult replay_jsonl(std::string_view jsonl, const Verifier& verifier) const;
};

}  // namespace pv
