// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

namespace pv {

struct Signal {
    std::string id;
    std::string entity;
    std::string kind;
    double score{0.0};
    std::string explanation;
    std::vector<std::string> evidence_event_ids;

    friend bool operator==(const Signal&, const Signal&) = default;
};

}  // namespace pv
