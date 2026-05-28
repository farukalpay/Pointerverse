// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <expected>

#include "pv/category/morphism.hpp"

namespace pv {

enum class CompositionError {
    DomainCodomainMismatch,
    LawConflict,
    ObserverLeak,
    NonDeterministicBoundary,
    InvalidPartiality,
    BrokenInvariant
};

[[nodiscard]] std::expected<ComposedMorphism, CompositionError>
compose(const Morphism& g, const Morphism& f);

[[nodiscard]] std::string to_string(CompositionError error);

}  // namespace pv
