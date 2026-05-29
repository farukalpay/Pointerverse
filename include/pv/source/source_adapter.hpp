// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <iosfwd>
#include <string>

#include "pv/source/source_batch.hpp"

namespace pv {

class SourceAdapter {
public:
    virtual ~SourceAdapter() = default;

    [[nodiscard]] virtual SourceBatch read(std::istream& input) const = 0;
};

class JsonlSourceAdapter final : public SourceAdapter {
public:
    explicit JsonlSourceAdapter(std::string default_source = "jsonl");

    [[nodiscard]] SourceBatch read(std::istream& input) const override;

private:
    std::string default_source_;
};

}  // namespace pv
