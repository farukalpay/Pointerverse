// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <iosfwd>
#include <vector>

#include "pv/ingest/ingestion_pipeline.hpp"
#include "pv/normalize/graph_event_encoder.hpp"
#include "pv/rule/rule.hpp"

namespace pv {

class IngestionIndex;
class Repository;

// Imports a generic graph-event stream (JSONL) into a repository branch. Each
// line describes one typed edge:
//
//   {"id":"1","from":"Suleiman_I","from_type":"Sultan",
//    "to":"OttomanArmy","to_type":"Army","relation":"commands","troops":50000}
//
// Objects are created on first reference, the edge carries any extra scalar
// fields as typed attributes, and each event becomes one content-addressed
// commit. This is the general counterpart to the agent-log audit pipeline: any
// event stream can be turned into a forkable, verifiable world.
class GraphLogImporter {
public:
    explicit GraphLogImporter(Repository& repository);

    [[nodiscard]] IngestionResult import(
        std::istream& input,
        IngestionIndex& index,
        const IngestionOptions& options,
        const std::vector<Rule>& rules = {});

    [[nodiscard]] IngestionResult import(
        const std::vector<GraphEvent>& events,
        IngestionIndex& index,
        const IngestionOptions& options,
        const std::vector<Rule>& rules = {});

private:
    Repository& repository_;
};

}  // namespace pv
