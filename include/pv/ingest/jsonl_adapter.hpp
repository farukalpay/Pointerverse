// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include "pv/ingest/adapter.hpp"

namespace pv {

class JsonlEvidenceAdapter final : public EvidenceAdapter {
public:
    explicit JsonlEvidenceAdapter(std::string default_source = "agent-log");

    [[nodiscard]] EvidenceBatch read(std::istream& input) const override;

private:
    std::string default_source_;
};

}  // namespace pv
