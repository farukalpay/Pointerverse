// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "pv/ingest/evidence.hpp"
#include "pv/law/verifier.hpp"

namespace pv {

class EvidenceNormalizer;
class IngestionIndex;
class Repository;

struct IngestionOptions {
    std::string branch{"main"};
    std::string domain{"agent_audit"};
    VerificationMode mode{VerificationMode::Observe};
};

struct IngestionMessage {
    std::size_t line{0};
    std::string event_id;
    std::string message;
};

struct IngestionResult {
    std::size_t events_read{0};
    std::size_t accepted{0};
    std::size_t skipped_duplicates{0};
    std::size_t rejected{0};
    std::size_t errors{0};
    std::size_t violations{0};
    std::vector<IngestionMessage> messages;
};

class IngestionPipeline {
public:
    explicit IngestionPipeline(Repository& repository);

    [[nodiscard]] IngestionResult ingest(
        const std::vector<EvidenceEvent>& events,
        const EvidenceNormalizer& normalizer,
        IngestionIndex& index,
        const IngestionOptions& options);

private:
    Repository& repository_;
};

}  // namespace pv
