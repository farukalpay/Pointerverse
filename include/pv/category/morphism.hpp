// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "pv/core/delta.hpp"
#include "pv/core/pointer.hpp"
#include "pv/core/relation.hpp"
#include "pv/core/snapshot.hpp"
#include "pv/core/type.hpp"

namespace pv {

struct MorphismSignature {
    TypeId domain;
    TypeId codomain;

    friend bool operator==(MorphismSignature, MorphismSignature) = default;
};

// One term of a morphism `set` expression: OPERAND (OP OPERAND)*, evaluated left
// to right. The first term has op '\0'; later terms carry + - * /. An operand is
// either a numeric literal or the name of a numeric attribute on the object.
struct MorphismExprTerm {
    char op{'\0'};
    bool literal{false};
    double value{0.0};
    std::string attribute;
};

// `set KEY = EXPR`: recompute a numeric attribute on the transformed object.
struct MorphismSetAttribute {
    std::string key;
    std::vector<MorphismExprTerm> expr;
};

// `emit self -> TARGET : REL` (or `emit TARGET -> self : REL`): create an edge
// between the transformed object and a named object when the morphism is applied.
struct MorphismEmitEdge {
    std::string target;
    std::string relation;
    CausalRole role{CausalRole::Generative};
    double weight{1.0};
    bool reverse{false};
};

using MorphismAction = std::variant<MorphismSetAttribute, MorphismEmitEdge>;

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

    // Attach a transformation the morphism performs on each selected object, beyond
    // retyping: a `set` attribute update or an `emit` edge. Applied in order.
    void add_action(MorphismAction action);
    [[nodiscard]] const std::vector<MorphismAction>& actions() const noexcept;

private:
    std::string name_;
    MorphismSignature signature_;
    std::vector<MorphismAction> actions_;
};

class ComposedMorphism final : public Morphism {
public:
    ComposedMorphism(
        std::string name,
        MorphismSignature signature,
        std::shared_ptr<const Morphism> first,
        std::shared_ptr<const Morphism> second);

    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] MorphismSignature signature() const override;
    [[nodiscard]] Delta apply(const WorldSnapshot& snapshot, const Selection& selection) const override;

private:
    std::string name_;
    MorphismSignature signature_;
    std::shared_ptr<const Morphism> first_;
    std::shared_ptr<const Morphism> second_;
};

}  // namespace pv
