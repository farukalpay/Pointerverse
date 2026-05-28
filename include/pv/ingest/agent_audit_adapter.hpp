// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pv/ingest/normalizer.hpp"

namespace pv {

class AgentAuditAdapter final : public EvidenceNormalizer {
public:
    [[nodiscard]] NormalizedAuditEvent normalize(const EvidenceEvent& event) const override;
};

}  // namespace pv
