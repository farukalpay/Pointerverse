#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "pointerverse/law.hpp"
#include "pointerverse/model.hpp"
#include "pointerverse/morphism.hpp"
#include "pointerverse/object.hpp"
#include "pointerverse/trace.hpp"

namespace pointerverse {

struct EvolveResult {
    std::size_t requested_steps{0};
    std::size_t completed_steps{0};
    bool passed{true};
    std::vector<LawResult> law_results;
    std::vector<std::string> events;
    std::vector<TraceEvent> structured_events;
    std::size_t regions_formed{0};
    double max_pressure{0.0};
};

class World {
public:
    [[nodiscard]] ObjectHandle create_object(
        std::string type,
        std::string name,
        std::map<std::string, std::string> config = {});

    [[nodiscard]] RelationId link(
        ObjectHandle source,
        ObjectHandle target,
        std::string relation,
        double weight = 1.0,
        CausalTag causality = CausalTag::Structural);

    void set_state(ObjectHandle handle, StateVector state);

    [[nodiscard]] MorphismId register_morphism(
        std::string name,
        std::string from_type,
        std::string to_type,
        std::string effect = "identity");

    [[nodiscard]] CompositionResult compose(MorphismId outer, MorphismId inner) const;
    [[nodiscard]] CompositionResult compose(const std::string& outer_name, const std::string& inner_name) const;

    [[nodiscard]] InternalizationResult seed_contradiction(
        std::size_t count,
        std::map<std::string, std::string> options = {});
    [[nodiscard]] InternalizationResult ingest_external_event(const ExternalEvent& event);
    [[nodiscard]] MorphismApplicationResult apply_morphism(MorphismId morphism, ObjectHandle target);
    [[nodiscard]] MorphismApplicationResult apply_morphism(
        const std::string& morphism_name,
        const std::string& target_name);

    void add_law(Law law);
    void add_builtin_law(const std::string& name, double tolerance = 1e-9);

    [[nodiscard]] EvolveResult evolve(std::size_t steps);
    [[nodiscard]] WorldSnapshot snapshot() const;

    [[nodiscard]] bool contains(ObjectHandle handle) const noexcept;
    [[nodiscard]] const Object& object(ObjectHandle handle) const;
    [[nodiscard]] Object& object(ObjectHandle handle);
    [[nodiscard]] ObjectHandle object_by_name(const std::string& name) const;
    [[nodiscard]] bool has_object(const std::string& name) const noexcept;

    [[nodiscard]] const RelationPointer& relation(RelationId id) const;
    [[nodiscard]] const Morphism& morphism(MorphismId id) const;
    [[nodiscard]] MorphismId morphism_by_name(const std::string& name) const;
    [[nodiscard]] bool has_morphism(const std::string& name) const noexcept;

    [[nodiscard]] const std::vector<RelationPointer>& relations() const noexcept;
    [[nodiscard]] const std::vector<Morphism>& morphisms() const noexcept;
    [[nodiscard]] const std::vector<Region>& regions() const noexcept;
    [[nodiscard]] const Region& region(RegionId id) const;
    [[nodiscard]] const Region& region_by_name(const std::string& name) const;
    [[nodiscard]] StructuralPressure pressure(ObjectHandle target) const;
    [[nodiscard]] std::string inspect_world() const;
    [[nodiscard]] std::string inspect_region(RegionId id) const;
    [[nodiscard]] std::string trace_origin(RegionId id) const;
    [[nodiscard]] const std::vector<Law>& laws() const noexcept;
    [[nodiscard]] const Trace& trace() const noexcept;
    [[nodiscard]] std::uint64_t time() const noexcept;

private:
    [[nodiscard]] std::optional<std::size_t> relation_index(RelationId id) const noexcept;
    [[nodiscard]] std::optional<std::size_t> morphism_index(MorphismId id) const noexcept;
    [[nodiscard]] std::optional<std::size_t> region_index(RegionId id) const noexcept;
    void apply_rewrite(std::vector<std::string>& events, std::vector<TraceEvent>& structured_events);
    [[nodiscard]] std::vector<LawResult> check_laws(const WorldSnapshot& before, const WorldSnapshot& after) const;
    [[nodiscard]] double estimate_graph_entropy() const;
    [[nodiscard]] double estimate_law_residual(const std::vector<LawResult>& law_results) const;
    [[nodiscard]] double estimate_state_drift(const WorldSnapshot& before, const WorldSnapshot& after) const;
    [[nodiscard]] double estimate_noncommutativity(const Morphism& morphism, const Object& object) const;
    void add_pressure(ObjectHandle target, const StructuralPressure& pressure);
    [[nodiscard]] std::size_t form_regions(std::vector<TraceEvent>& structured_events);
    [[nodiscard]] RegionBoundary calculate_boundary(const std::vector<ObjectHandle>& handles) const;
    [[nodiscard]] bool is_internal_relation(const RelationPointer& relation, const std::vector<ObjectHandle>& handles) const;

    std::vector<Object> objects_;
    std::unordered_map<std::string, ObjectHandle> names_;
    std::vector<RelationPointer> relations_;
    std::vector<Morphism> morphisms_;
    std::unordered_map<std::string, MorphismId> morphism_names_;
    std::vector<StructuralPressure> object_pressures_;
    std::vector<Region> regions_;
    std::vector<Law> laws_;
    Trace trace_;
    std::uint64_t time_{0};
    std::uint64_t next_relation_id_{1};
    std::uint64_t next_morphism_id_{1};
    std::uint64_t next_region_id_{1};
};

}  // namespace pointerverse
