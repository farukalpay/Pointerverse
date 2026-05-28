// SPDX-License-Identifier: Apache-2.0
#include "pv/category/composition.hpp"

#include <fmt/format.h>

namespace pv {

IdentityMorphism::IdentityMorphism(TypeId type) : type_(type) {}

std::string_view IdentityMorphism::name() const {
    return "id";
}

MorphismSignature IdentityMorphism::signature() const {
    return MorphismSignature{type_, type_};
}

Delta IdentityMorphism::apply(const WorldSnapshot&, const Selection&) const {
    Delta delta;
    delta.events.push_back(TraceEvent{
        {},
        "morphism.apply",
        {{"name", "id"}, {"kind", "identity"}},
        {{"delta_size", 0.0}}
    });
    return delta;
}

DefinedMorphism::DefinedMorphism(std::string name, MorphismSignature signature)
    : name_(std::move(name)), signature_(signature) {}

std::string_view DefinedMorphism::name() const {
    return name_;
}

MorphismSignature DefinedMorphism::signature() const {
    return signature_;
}

Delta DefinedMorphism::apply(const WorldSnapshot& snapshot, const Selection& selection) const {
    Delta delta;
    for (const auto object : selection.objects) {
        const auto* view = snapshot.object(object);
        if (view == nullptr || view->type != signature_.domain) {
            continue;
        }
        if (signature_.domain != signature_.codomain) {
            delta.updates.push_back(ObjectUpdate{object, signature_.codomain, std::nullopt});
        }
    }
    delta.events.push_back(TraceEvent{
        {},
        "morphism.apply",
        {{"name", name_}},
        {{"selected_objects", static_cast<double>(selection.objects.size())}}
    });
    return delta;
}

ComposedMorphism::ComposedMorphism(std::string name, MorphismSignature signature)
    : name_(std::move(name)), signature_(signature) {}

std::string_view ComposedMorphism::name() const {
    return name_;
}

MorphismSignature ComposedMorphism::signature() const {
    return signature_;
}

Delta ComposedMorphism::apply(const WorldSnapshot&, const Selection& selection) const {
    Delta delta;
    delta.events.push_back(TraceEvent{
        {},
        "morphism.apply",
        {{"name", name_}, {"kind", "composition"}},
        {{"selected_objects", static_cast<double>(selection.objects.size())}}
    });
    return delta;
}

std::expected<ComposedMorphism, CompositionError> compose(const Morphism& g, const Morphism& f) {
    const auto g_signature = g.signature();
    const auto f_signature = f.signature();
    if (f_signature.codomain != g_signature.domain) {
        return std::unexpected(CompositionError::DomainCodomainMismatch);
    }
    return ComposedMorphism{
        fmt::format("{} after {}", g.name(), f.name()),
        MorphismSignature{f_signature.domain, g_signature.codomain}
    };
}

std::string to_string(CompositionError error) {
    switch (error) {
    case CompositionError::DomainCodomainMismatch:
        return "DomainCodomainMismatch";
    case CompositionError::LawConflict:
        return "LawConflict";
    case CompositionError::ObserverLeak:
        return "ObserverLeak";
    case CompositionError::NonDeterministicBoundary:
        return "NonDeterministicBoundary";
    case CompositionError::InvalidPartiality:
        return "InvalidPartiality";
    case CompositionError::BrokenInvariant:
        return "BrokenInvariant";
    }
    return "BrokenInvariant";
}

}  // namespace pv
