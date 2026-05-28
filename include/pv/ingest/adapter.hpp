// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

#include "pv/ingest/evidence.hpp"

namespace pv {

struct EvidenceReadError {
    std::size_t line{0};
    std::string message;
};

struct EvidenceBatch {
    std::vector<EvidenceEvent> events;
    std::vector<EvidenceReadError> errors;
};

class EvidenceAdapter {
public:
    virtual ~EvidenceAdapter() = default;

    [[nodiscard]] virtual EvidenceBatch read(std::istream& input) const = 0;
};

}  // namespace pv
