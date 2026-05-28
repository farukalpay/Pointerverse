// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "pv/sentinel/heartbeat.hpp"
#include "pv/sentinel/report.hpp"

namespace pv {

class SentinelRuntime {
public:
    explicit SentinelRuntime(std::filesystem::path root);

    [[nodiscard]] SentinelReport run_once();
    [[nodiscard]] SentinelReport tick();
    [[nodiscard]] const std::vector<Heartbeat>& heartbeats() const noexcept;

private:
    std::filesystem::path root_;
    std::uint64_t tick_{0};
    std::vector<Heartbeat> heartbeats_;
};

}  // namespace pv
