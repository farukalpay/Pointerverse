// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "pv/hash/canonical.hpp"
#include "pv/measure/graph_view.hpp"

namespace pv {

struct FunctionalResult {
    std::uint64_t value{0};
    std::vector<ObjectId> witness_objects;
    std::vector<PointerId> witness_pointers;
    std::string explanation;
};

struct PropagationOptions {
    std::uint32_t max_steps{8};
    std::uint64_t attenuation_num{0};
    std::uint64_t attenuation_den{0};
};

struct PathMultiplicityOptions {
    std::uint32_t max_depth{6};
    std::uint64_t max_paths{100000};
    bool include_length_zero{false};
    std::uint64_t attenuation_num{0};
    std::uint64_t attenuation_den{0};
};

class GraphFunctional {
public:
    virtual ~GraphFunctional() = default;

    [[nodiscard]] virtual std::string_view name() const = 0;

    [[nodiscard]] virtual FunctionalResult evaluate(
        const WeightedGraphView& graph,
        std::span<const ObjectId> seeds) const = 0;
};

class ForwardConeMass final : public GraphFunctional {
public:
    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] FunctionalResult evaluate(
        const WeightedGraphView& graph,
        std::span<const ObjectId> seeds) const override;
};

class ReverseDependencyMass final : public GraphFunctional {
public:
    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] FunctionalResult evaluate(
        const WeightedGraphView& graph,
        std::span<const ObjectId> seeds) const override;
};

class CutVertexImpact final : public GraphFunctional {
public:
    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] FunctionalResult evaluate(
        const WeightedGraphView& graph,
        std::span<const ObjectId> seeds) const override;
};

class PathMultiplicity final : public GraphFunctional {
public:
    explicit PathMultiplicity(PathMultiplicityOptions options = {});

    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] FunctionalResult evaluate(
        const WeightedGraphView& graph,
        std::span<const ObjectId> seeds) const override;

private:
    PathMultiplicityOptions options_;
};

class BoundaryExpansion final : public GraphFunctional {
public:
    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] FunctionalResult evaluate(
        const WeightedGraphView& graph,
        std::span<const ObjectId> seeds) const override;
};

class PropagatedMass final : public GraphFunctional {
public:
    explicit PropagatedMass(PropagationOptions options = {});

    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] FunctionalResult evaluate(
        const WeightedGraphView& graph,
        std::span<const ObjectId> seeds) const override;

private:
    PropagationOptions options_;
};

[[nodiscard]] Hash256 functional_result_hash(std::string_view name, FunctionalResult result);

}  // namespace pv
