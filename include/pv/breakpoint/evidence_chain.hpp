// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "pv/breakpoint/breakpoint.hpp"

namespace pv {

class ProjectionStore;

struct EvidenceChainStep {
    CommitId commit;
    Epoch epoch;
    std::string event;
    std::string detail;
    std::string evidence_id;
};

struct EvidenceChain {
    Breakpoint breakpoint;
    EvidenceChainStep triggering_event;
    std::vector<EvidenceChainStep> prior_enabling_events;
    std::vector<std::string> affected_entities;
    std::vector<std::string> affected_relations;
    std::vector<std::string> evidence_ids;
};

class EvidenceChainBuilder {
public:
    [[nodiscard]] EvidenceChain build(
        const ProjectionStore& store,
        std::string_view branch,
        const Breakpoint& breakpoint) const;
};

[[nodiscard]] std::string render_evidence_chain_text(const EvidenceChain& chain);

}  // namespace pv
