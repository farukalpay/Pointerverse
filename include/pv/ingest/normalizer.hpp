// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pv/ingest/evidence.hpp"

namespace pv {

class EvidenceNormalizer {
public:
    virtual ~EvidenceNormalizer();

    [[nodiscard]] virtual NormalizedAuditEvent normalize(const EvidenceEvent& event) const = 0;
};

}  // namespace pv
