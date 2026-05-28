// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <expected>
#include <memory>

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

[[nodiscard]] std::expected<std::shared_ptr<const Morphism>, CompositionError>
compose(std::shared_ptr<const Morphism> g, std::shared_ptr<const Morphism> f);

[[nodiscard]] std::string to_string(CompositionError error);

}  // namespace pv
