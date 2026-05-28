// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "pv/core/delta.hpp"
#include "pv/core/pointer.hpp"
#include "pv/core/snapshot.hpp"
#include "pv/core/type.hpp"

namespace pv {

struct MorphismSignature {
    TypeId domain;
    TypeId codomain;

    friend bool operator==(MorphismSignature, MorphismSignature) = default;
};

struct Selection {
    std::vector<ObjectId> objects;
    std::vector<PointerId> pointers;
};

class Morphism {
public:
    virtual ~Morphism() = default;

    [[nodiscard]] virtual std::string_view name() const = 0;
    [[nodiscard]] virtual MorphismSignature signature() const = 0;
    [[nodiscard]] virtual Delta apply(const WorldSnapshot& snapshot, const Selection& selection) const = 0;
};

class IdentityMorphism final : public Morphism {
public:
    explicit IdentityMorphism(TypeId type);

    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] MorphismSignature signature() const override;
    [[nodiscard]] Delta apply(const WorldSnapshot& snapshot, const Selection& selection) const override;

private:
    TypeId type_;
};

class DefinedMorphism final : public Morphism {
public:
    DefinedMorphism(std::string name, MorphismSignature signature);

    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] MorphismSignature signature() const override;
    [[nodiscard]] Delta apply(const WorldSnapshot& snapshot, const Selection& selection) const override;

private:
    std::string name_;
    MorphismSignature signature_;
};

class ComposedMorphism final : public Morphism {
public:
    ComposedMorphism(std::string name, MorphismSignature signature);

    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] MorphismSignature signature() const override;
    [[nodiscard]] Delta apply(const WorldSnapshot& snapshot, const Selection& selection) const override;

private:
    std::string name_;
    MorphismSignature signature_;
};

}  // namespace pv
