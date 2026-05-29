// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pv/normalize/canonical_event.hpp"
#include "pv/source/source_event.hpp"

namespace pv {

class EventNormalizer {
public:
    virtual ~EventNormalizer() = default;

    [[nodiscard]] virtual CanonicalEvent normalize(const SourceEvent& event) const = 0;
};

class SourceEventNormalizer final : public EventNormalizer {
public:
    [[nodiscard]] CanonicalEvent normalize(const SourceEvent& event) const override;
};

}  // namespace pv
