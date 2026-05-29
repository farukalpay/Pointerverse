// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pv/core/delta.hpp"
#include "pv/core/relation.hpp"
#include "pv/core/world.hpp"

namespace pv {

// A body atom of a derivation: link FROM_VAR -> TO_VAR : RELATION. FROM_VAR and
// TO_VAR are variable names (any identifier); RELATION is a literal relation name.
struct DeriveAtom {
    std::string from_var;
    std::string relation;
    std::string to_var;
};

// The head of a derivation: the edge it produces from variables bound by the body.
struct DeriveHead {
    std::string from_var;
    std::string relation;
    std::string to_var;
    CausalRole role{CausalRole::Structural};
    double weight{1.0};
};

// A bounded forward-chaining rule:
//
//   derive transitive_reach
//   from link X -> Y : reaches
//   from link Y -> Z : reaches
//   make link X -> Z : reaches role=Structural weight=1.0
//
// Body atoms are joined on shared variables; the head edge is produced for every
// satisfying binding. Derivations are run to a fixpoint by `evolve` (see
// DerivationEvolution) over the current graph, so they express relational closures
// (reachability, transitive citation, ...) that a single-hop rule cannot.
struct Derivation {
    std::string name;
    std::vector<DeriveAtom> body;
    DeriveHead head;
};

// Line-oriented builder, mirroring RuleBuilder: `derive NAME` opens a block, `from`
// lines append body atoms, and a `make` line sets the head and completes the block.
class DerivationBuilder {
public:
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] const std::string& name() const noexcept;

    [[nodiscard]] std::optional<Derivation> consume_line(std::string_view line);
    void reset() noexcept;

private:
    Derivation draft_;
    bool active_{false};
};

[[nodiscard]] bool is_derivation_command(std::string_view command) noexcept;
[[nodiscard]] std::vector<Derivation> parse_derivations(std::string_view text);

// An evolution rule that, in one `evolve` step, clears the previously derived edges
// and recomputes the closure of all declared derivations against the current graph.
// Derived edges carry law_domain "derivation"; the step is deterministic and
// idempotent (re-running `evolve` reproduces the same edges). With no derivations
// declared it is a no-op, so worlds that do not use `derive` evolve exactly as before.
class DerivationEvolution final : public EvolutionRule {
public:
    explicit DerivationEvolution(std::vector<Derivation> derivations);

    [[nodiscard]] Delta step(const WorldSnapshot& snapshot, Epoch next) const override;

private:
    std::vector<Derivation> derivations_;
};

}  // namespace pv
