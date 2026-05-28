#include "pointerverse/world.hpp"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <limits>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>

namespace pointerverse {
namespace {

std::size_t parse_dimension(const std::map<std::string, std::string>& config) {
    const auto iter = config.find("dim");
    if (iter == config.end()) {
        return 0;
    }
    try {
        return static_cast<std::size_t>(std::stoull(iter->second));
    } catch (const std::exception&) {
        throw std::invalid_argument("object dim must be a positive integer");
    }
}

bool is_causal_weight_negative(const RelationPointer& relation) {
    return relation.causality == CausalTag::Causal && relation.weight < 0.0;
}

double clamp01(double value) {
    if (!std::isfinite(value)) {
        return 0.0;
    }
    return std::clamp(value, 0.0, 1.0);
}

std::string unique_name(const std::string& base, const std::unordered_map<std::string, ObjectHandle>& names) {
    if (!names.contains(base)) {
        return base;
    }

    for (std::size_t suffix = 1;; ++suffix) {
        const auto candidate = fmt::format("{}_{}", base, suffix);
        if (!names.contains(candidate)) {
            return candidate;
        }
    }
}

bool relation_is_pressure_source(const RelationPointer& relation) {
    return relation.relation == "contradicts"
        || relation.relation == "requires_relation"
        || relation.relation == "composition_failed"
        || relation.causality == CausalTag::Contradictory;
}

}  // namespace

ObjectHandle World::create_object(std::string type, std::string name, std::map<std::string, std::string> config) {
    if (name.empty()) {
        throw std::invalid_argument("object name cannot be empty");
    }
    if (type.empty()) {
        throw std::invalid_argument("object type cannot be empty");
    }
    if (names_.contains(name)) {
        throw std::invalid_argument(fmt::format("object '{}' already exists", name));
    }

    Object object;
    object.handle = ObjectHandle{
        static_cast<std::uint32_t>(objects_.size()),
        1
    };
    object.name = std::move(name);
    object.type = std::move(type);
    object.config = std::move(config);

    const auto dimension = parse_dimension(object.config);
    if (dimension > 0) {
        object.state = StateVector::basis(dimension);
    }

    const auto handle = object.handle;
    names_.emplace(object.name, handle);
    objects_.push_back(std::move(object));
    object_pressures_.push_back({});
    return handle;
}

RelationId World::link(
    ObjectHandle source,
    ObjectHandle target,
    std::string relation,
    double weight,
    CausalTag causality) {
    if (!contains(source)) {
        throw std::invalid_argument(fmt::format("invalid source handle {}", to_string(source)));
    }
    if (!contains(target)) {
        throw std::invalid_argument(fmt::format("invalid target handle {}", to_string(target)));
    }
    if (relation.empty()) {
        throw std::invalid_argument("relation name cannot be empty");
    }
    if (!std::isfinite(weight)) {
        throw std::invalid_argument("relation weight must be finite");
    }

    RelationPointer pointer;
    pointer.id = RelationId{next_relation_id_++};
    pointer.source = source;
    pointer.target = target;
    pointer.relation = std::move(relation);
    pointer.weight = weight;
    pointer.causality = causality;

    const auto id = pointer.id;
    relations_.push_back(std::move(pointer));
    object(source).outgoing.push_back(id);
    object(target).incoming.push_back(id);
    return id;
}

void World::set_state(ObjectHandle handle, StateVector state) {
    if (!state.empty() && !state.normalize()) {
        throw std::invalid_argument("state vector cannot be normalized");
    }
    object(handle).state = std::move(state);
}

MorphismId World::register_morphism(
    std::string name,
    std::string from_type,
    std::string to_type,
    std::string effect) {
    if (name.empty()) {
        throw std::invalid_argument("morphism name cannot be empty");
    }
    if (from_type.empty() || to_type.empty()) {
        throw std::invalid_argument("morphism types cannot be empty");
    }
    if (morphism_names_.contains(name)) {
        throw std::invalid_argument(fmt::format("morphism '{}' already exists", name));
    }

    Morphism morphism;
    morphism.id = MorphismId{next_morphism_id_++};
    morphism.name = std::move(name);
    morphism.from_type = std::move(from_type);
    morphism.to_type = std::move(to_type);
    morphism.effect = effect.empty() ? "identity" : std::move(effect);

    const auto id = morphism.id;
    morphism_names_.emplace(morphism.name, id);
    morphisms_.push_back(std::move(morphism));
    return id;
}

CompositionResult World::compose(MorphismId outer_id, MorphismId inner_id) const {
    const auto& outer = morphism(outer_id);
    const auto& inner = morphism(inner_id);

    CompositionResult result;
    result.name = fmt::format("{}_after_{}", outer.name, inner.name);
    result.from_type = inner.from_type;
    result.to_type = outer.to_type;
    result.valid = inner.to_type == outer.from_type;
    result.weakly_valid = result.valid
        && ((outer.effect == "stabilize" && inner.effect == "split")
            || (outer.effect == "invert" && inner.effect == "project")
            || (outer.effect == "project" && inner.effect == "invert"));
    result.law_residual_delta = result.weakly_valid ? 0.05 : 0.0;

    if (!result.valid) {
        result.errors.push_back(fmt::format(
            "type mismatch: inner '{}' returns '{}', outer '{}' expects '{}'",
            inner.name,
            inner.to_type,
            outer.name,
            outer.from_type));
    } else if (result.weakly_valid) {
        result.warnings.push_back("law preservation is uncertain under this composition");
    }

    return result;
}

CompositionResult World::compose(const std::string& outer_name, const std::string& inner_name) const {
    return compose(morphism_by_name(outer_name), morphism_by_name(inner_name));
}

InternalizationResult World::seed_contradiction(std::size_t count, std::map<std::string, std::string> options) {
    if (count == 0) {
        throw std::invalid_argument("seed contradiction count must be greater than zero");
    }

    const auto before = snapshot();
    InternalizationResult result;
    std::vector<TraceEvent> structured_events;
    const auto prefix = options.contains("prefix") ? options.at("prefix") : std::string{"C"};

    for (std::size_t index = 0; index < count; ++index) {
        ExternalEvent event;
        event.id = unique_name(fmt::format("{}{}", prefix, index), names_);
        event.kind = "conflict";
        event.weight = 1.0;
        event.metrics = {
            {"law_residual", 1.0},
            {"graph_entropy", 0.60},
            {"state_drift", 0.80},
            {"noncommutativity", 0.60}
        };

        auto compression = compress_external_event(event);
        const auto handle = create_object(
            compression.object_type,
            compression.symbol_name,
            {{"source", "seed"}, {"kind", event.kind}});
        set_state(handle, StateVector{{Scalar{0.70710678118, 0.0}, Scalar{0.70710678118, 0.0}}});
        add_pressure(handle, compression.pressure);
        result.objects.push_back(handle);
        result.pressure.magnitude = std::max(result.pressure.magnitude, compression.pressure.magnitude);
        result.pressure.instability = std::max(result.pressure.instability, compression.pressure.instability);
        result.events.push_back(fmt::format("{} -> {}", event.kind, compression.object_type));
    }

    for (std::size_t index = 0; index < result.objects.size(); ++index) {
        const auto next = result.objects[(index + 1) % result.objects.size()];
        const auto id = link(result.objects[index], next, "contradicts", 0.9, CausalTag::Contradictory);
        result.pointers.push_back(id);
    }

    ++time_;
    const auto after = snapshot();
    structured_events.push_back(TraceEvent{
        time_,
        "symbolic_seed",
        {},
        {},
        {},
        {},
        {},
        after.max_pressure - before.max_pressure,
        {},
        0.0,
        fmt::format("seeded {} unresolved constraint object(s)", count)
    });
    trace_.append(TraceStep{
        time_,
        "symbolic_internalization",
        before,
        after,
        check_laws(before, after),
        result.events,
        structured_events
    });

    return result;
}

InternalizationResult World::ingest_external_event(const ExternalEvent& event) {
    const auto before = snapshot();
    const auto compression = compress_external_event(event);
    InternalizationResult result;

    const auto symbol_name = unique_name(compression.symbol_name, names_);
    std::map<std::string, std::string> config{
        {"source", "external"},
        {"kind", event.kind},
        {"interpretation", compression.interpretation}
    };
    for (const auto& tag : event.tags) {
        config.emplace(fmt::format("tag.{}", tag), "true");
    }

    const auto source = create_object(compression.object_type, symbol_name, std::move(config));
    set_state(source, StateVector{{Scalar{event.weight, 0.0}, Scalar{1.0 - clamp01(event.weight), 0.0}}});
    add_pressure(source, compression.pressure);
    result.objects.push_back(source);
    result.pressure = compression.pressure;
    result.events.push_back(compression.interpretation);

    for (const auto& external_link : event.links) {
        if (external_link.to.empty()) {
            continue;
        }
        const auto target = has_object(external_link.to)
            ? object_by_name(external_link.to)
            : create_object("ExternalSymbol", unique_name(external_link.to, names_), {{"source", "external_link"}});
        result.pointers.push_back(link(
            source,
            target,
            external_link.relation,
            external_link.weight,
            CausalTag::Structural));
    }

    ++time_;
    const auto after = snapshot();
    const TraceEvent trace_event{
        time_,
        "symbolic_internalization",
        {object(source).name},
        {},
        {},
        {},
        {},
        after.max_pressure - before.max_pressure,
        {},
        0.0,
        compression.interpretation
    };

    trace_.append(TraceStep{
        time_,
        "symbolic_internalization",
        before,
        after,
        check_laws(before, after),
        result.events,
        {trace_event}
    });

    return result;
}

MorphismApplicationResult World::apply_morphism(MorphismId morphism_id, ObjectHandle target_handle) {
    const auto before = snapshot();
    const auto saved_objects = objects_;
    const auto saved_names = names_;
    const auto saved_relations = relations_;
    const auto saved_pressures = object_pressures_;
    const auto saved_regions = regions_;
    const auto saved_next_relation_id = next_relation_id_;
    const auto saved_next_region_id = next_region_id_;

    const auto& selected_morphism = morphism(morphism_id);
    const auto target_before = object(target_handle);

    MorphismApplicationResult result;
    result.morphism = selected_morphism.name;
    result.target = target_before.name;

    std::vector<std::string> events;
    std::vector<TraceEvent> structured_events;
    std::map<std::string, double> residuals;

    const auto fail_without_mutation = [&](std::string reason, double residual, double pressure_delta) {
        objects_ = saved_objects;
        names_ = saved_names;
        relations_ = saved_relations;
        object_pressures_ = saved_pressures;
        regions_ = saved_regions;
        next_relation_id_ = saved_next_relation_id;
        next_region_id_ = saved_next_region_id;

        result.valid = false;
        result.applied = false;
        result.reason = std::move(reason);
        result.law_residual = residual;
        result.pressure_delta = pressure_delta;
        result.counterfactual_delta = estimate_noncommutativity(selected_morphism, target_before);
        result.events.clear();
        result.events.push_back(result.reason);

        add_pressure(target_handle, compute_pressure(residual, estimate_graph_entropy(), 0.0, result.counterfactual_delta));

        ++time_;
        const auto after_failure = snapshot();
        structured_events.push_back(TraceEvent{
            time_,
            "morphism_rejected",
            {target_before.name},
            {},
            {"type_consistency", "bounded_drift", "unresolved_pressure"},
            selected_morphism.name,
            {},
            result.pressure_delta,
            {{"law_residual", result.law_residual}},
            result.counterfactual_delta,
            result.reason
        });
        trace_.append(TraceStep{
            time_,
            "morphism_application",
            before,
            after_failure,
            check_laws(before, after_failure),
            result.events,
            structured_events
        });
    };

    if (target_before.type != selected_morphism.from_type) {
        fail_without_mutation(
            fmt::format("domain predicate failed: target type '{}' is not '{}'", target_before.type, selected_morphism.from_type),
            1.0,
            compute_pressure(1.0, estimate_graph_entropy(), 0.0, 0.0).magnitude);
        return result;
    }

    result.applied = true;

    auto& target = object(target_handle);
    const auto effect = selected_morphism.effect;
    if (effect == "split") {
        const auto target_name = target.name;
        const auto child_name = unique_name(fmt::format("{}_part", target_name), names_);
        const auto child = create_object(selected_morphism.to_type, child_name, {{"origin", target_name}});
        const auto relation_id = link(target_handle, child, "transforms_into", 0.6, CausalTag::Emergent);
        result.events.push_back(fmt::format("split produced {} through {}", child_name, to_string(relation_id)));
    } else if (effect == "stabilize") {
        target.state.normalize();
        object_pressures_[target_handle.slot].magnitude = std::max(0.0, object_pressures_[target_handle.slot].magnitude - 0.25);
        object_pressures_[target_handle.slot].instability = std::max(0.0, object_pressures_[target_handle.slot].instability - 0.20);
        result.events.push_back("stabilized target state and reduced local pressure");
    } else if (effect == "invert") {
        auto& amplitudes = target.state.amplitudes();
        std::reverse(amplitudes.begin(), amplitudes.end());
        target.state.normalize();
        result.events.push_back("inverted target state ordering");
    } else if (effect == "compress") {
        target.type = selected_morphism.to_type;
        target.state.normalize();
        object_pressures_[target_handle.slot].magnitude = std::max(0.0, object_pressures_[target_handle.slot].magnitude - 0.10);
        result.events.push_back("compressed target into codomain type");
    } else if (effect == "project" || effect == "measure") {
        target.type = selected_morphism.to_type;
        target.state.normalize();
        result.events.push_back("projected target into codomain type");
    } else {
        auto& amplitudes = target.state.amplitudes();
        for (std::size_t index = 0; index < amplitudes.size(); ++index) {
            amplitudes[index] *= std::polar(1.0, 0.017 * static_cast<double>(index + 1));
        }
        target.state.normalize();
        if (selected_morphism.to_type != selected_morphism.from_type) {
            target.type = selected_morphism.to_type;
        }
        result.events.push_back("applied generic traceable transition");
    }

    const auto after_candidate = snapshot();
    const auto law_results = check_laws(before, after_candidate);
    result.law_residual = estimate_law_residual(law_results);
    result.counterfactual_delta = estimate_noncommutativity(selected_morphism, target_before);
    const auto state_drift = estimate_state_drift(before, after_candidate);
    const auto pressure = compute_pressure(result.law_residual, after_candidate.graph_entropy, state_drift, result.counterfactual_delta);
    result.pressure_delta = pressure.magnitude;

    residuals.emplace("law_residual", result.law_residual);
    residuals.emplace("state_drift", state_drift);
    residuals.emplace("noncommutativity", result.counterfactual_delta);

    if (result.law_residual > 0.25 || state_drift > 0.90 || after_candidate.max_pressure > 1.25) {
        fail_without_mutation("transition rejected by law residual or bounded drift validation", result.law_residual, pressure.magnitude);
        return result;
    }

    add_pressure(target_handle, pressure);
    result.valid = true;
    result.reason = "transition accepted";

    ++time_;
    structured_events.push_back(TraceEvent{
        time_,
        "morphism_applied",
        {result.target},
        {},
        {},
        selected_morphism.name,
        {},
        result.pressure_delta,
        residuals,
        result.counterfactual_delta,
        result.reason
    });
    const auto regions_formed = form_regions(structured_events);
    if (regions_formed > 0) {
        result.events.push_back(fmt::format("formed {} region(s)", regions_formed));
    }

    const auto after = snapshot();
    trace_.append(TraceStep{
        time_,
        "morphism_application",
        before,
        after,
        check_laws(before, after),
        result.events,
        structured_events
    });

    return result;
}

MorphismApplicationResult World::apply_morphism(const std::string& morphism_name, const std::string& target_name) {
    return apply_morphism(morphism_by_name(morphism_name), object_by_name(target_name));
}

void World::add_law(Law law) {
    if (law.name.empty()) {
        throw std::invalid_argument("law name cannot be empty");
    }
    if (!law.check) {
        throw std::invalid_argument("law check cannot be empty");
    }
    laws_.push_back(std::move(law));
}

void World::add_builtin_law(const std::string& name, double tolerance) {
    if (name == "normalization") {
        add_law(Law{
            "normalization",
            [tolerance](const WorldSnapshot&, const WorldSnapshot& after) {
                const auto passed = after.max_normalization_error <= tolerance;
                return LawResult{
                    "normalization",
                    passed,
                    after.max_normalization_error,
                    passed ? "all state vectors remain normalized" : "state vector normalization drift exceeded tolerance"
                };
            }
        });
        return;
    }

    if (name == "causality") {
        add_law(Law{
            "causality",
            [](const WorldSnapshot&, const WorldSnapshot& after) {
                const auto violation_count = after.invalid_relation_count + after.negative_causal_weight_count;
                return LawResult{
                    "causality",
                    violation_count == 0,
                    static_cast<double>(violation_count),
                    violation_count == 0 ? "all relation endpoints are valid" : "causal relation violations detected"
                };
            }
        });
        return;
    }

    if (name == "probability_mass") {
        add_law(Law{
            "probability_mass",
            [tolerance](const WorldSnapshot& before, const WorldSnapshot& after) {
                const auto drift = std::abs(after.total_probability_mass - before.total_probability_mass);
                return LawResult{
                    "probability_mass",
                    drift <= tolerance,
                    drift,
                    drift <= tolerance ? "total probability mass is conserved" : "total probability mass drift exceeded tolerance"
                };
            }
        });
        return;
    }

    if (name == "bounded_pressure") {
        add_law(Law{
            "bounded_pressure",
            [tolerance](const WorldSnapshot&, const WorldSnapshot& after) {
                const auto overage = std::max(0.0, after.max_pressure - (1.0 + tolerance));
                return LawResult{
                    "bounded_pressure",
                    overage <= tolerance,
                    overage,
                    overage <= tolerance ? "structural pressure remains bounded" : "structural pressure exceeded bound"
                };
            }
        });
        return;
    }

    if (name == "type_consistency") {
        add_law(Law{
            "type_consistency",
            [](const WorldSnapshot&, const WorldSnapshot& after) {
                return LawResult{
                    "type_consistency",
                    after.object_count > 0 || after.relation_count == 0,
                    after.object_count > 0 || after.relation_count == 0 ? 0.0 : 1.0,
                    "typed graph endpoints are represented by objects"
                };
            }
        });
        return;
    }

    throw std::invalid_argument(fmt::format("unknown builtin law '{}'", name));
}

EvolveResult World::evolve(std::size_t steps) {
    EvolveResult result;
    result.requested_steps = steps;

    for (std::size_t index = 0; index < steps; ++index) {
        const auto before = snapshot();
        std::vector<std::string> events;
        std::vector<TraceEvent> structured_events;

        apply_rewrite(events, structured_events);
        ++time_;
        const auto regions_before = regions_.size();
        const auto formed = form_regions(structured_events);
        (void)formed;

        const auto after = snapshot();
        auto law_results = check_laws(before, after);
        const auto passed = std::all_of(law_results.begin(), law_results.end(), [](const LawResult& law_result) {
            return law_result.passed;
        });

        trace_.append(TraceStep{
            time_,
            "discrete_phase_rewrite",
            before,
            after,
            law_results,
            events,
            structured_events
        });

        result.completed_steps += 1;
        result.passed = result.passed && passed;
        result.law_results = trace_.steps().back().law_results;
        result.events = trace_.steps().back().events;
        result.structured_events = trace_.steps().back().structured_events;
        result.regions_formed += regions_.size() - regions_before;
        result.max_pressure = after.max_pressure;
    }

    return result;
}

WorldSnapshot World::snapshot() const {
    WorldSnapshot snapshot;
    snapshot.time = time_;
    snapshot.object_count = objects_.size();
    snapshot.relation_count = relations_.size();
    snapshot.morphism_count = morphisms_.size();
    snapshot.law_count = laws_.size();
    snapshot.region_count = regions_.size();
    snapshot.graph_entropy = estimate_graph_entropy();

    for (const auto& relation : relations_) {
        snapshot.total_relation_weight += relation.weight;
        if (!contains(relation.source) || !contains(relation.target)) {
            snapshot.invalid_relation_count += 1;
        }
        if (is_causal_weight_negative(relation)) {
            snapshot.negative_causal_weight_count += 1;
        }
    }

    snapshot.objects.reserve(objects_.size());
    for (const auto& object : objects_) {
        ObjectSnapshot object_snapshot;
        object_snapshot.name = object.name;
        object_snapshot.type = object.type;
        object_snapshot.dimension = object.state.dimension();
        object_snapshot.norm = object.state.norm();
        object_snapshot.normalization_error = object.state.normalization_error();
        object_snapshot.entropy = object.state.entropy();
        object_snapshot.incoming_count = object.incoming.size();
        object_snapshot.outgoing_count = object.outgoing.size();

        if (!object.state.empty()) {
            snapshot.total_probability_mass += object.state.norm_squared();
            snapshot.max_normalization_error = std::max(
                snapshot.max_normalization_error,
                object_snapshot.normalization_error);
        }

        snapshot.objects.push_back(std::move(object_snapshot));
    }

    for (const auto& pressure : object_pressures_) {
        snapshot.total_pressure += pressure.magnitude;
        snapshot.max_pressure = std::max(snapshot.max_pressure, pressure.magnitude);
        if (pressure.persistence >= 3.0) {
            snapshot.persistent_constraint_count += 1;
        }
        if (pressure.recurrence >= 0.75) {
            snapshot.attractor_count += 1;
        }
    }

    if (!regions_.empty()) {
        double stability_sum = 0.0;
        for (const auto& region : regions_) {
            stability_sum += region.stability;
        }
        snapshot.average_region_stability = stability_sum / static_cast<double>(regions_.size());
    }

    return snapshot;
}

bool World::contains(ObjectHandle handle) const noexcept {
    return handle.is_valid_token()
        && handle.slot < objects_.size()
        && objects_[handle.slot].handle == handle;
}

const Object& World::object(ObjectHandle handle) const {
    if (!contains(handle)) {
        throw std::out_of_range(fmt::format("unknown object handle {}", to_string(handle)));
    }
    return objects_[handle.slot];
}

Object& World::object(ObjectHandle handle) {
    if (!contains(handle)) {
        throw std::out_of_range(fmt::format("unknown object handle {}", to_string(handle)));
    }
    return objects_[handle.slot];
}

ObjectHandle World::object_by_name(const std::string& name) const {
    const auto iter = names_.find(name);
    if (iter == names_.end()) {
        throw std::out_of_range(fmt::format("unknown object '{}'", name));
    }
    return iter->second;
}

bool World::has_object(const std::string& name) const noexcept {
    return names_.contains(name);
}

const RelationPointer& World::relation(RelationId id) const {
    const auto index = relation_index(id);
    if (!index.has_value()) {
        throw std::out_of_range(fmt::format("unknown relation {}", to_string(id)));
    }
    return relations_[*index];
}

const Morphism& World::morphism(MorphismId id) const {
    const auto index = morphism_index(id);
    if (!index.has_value()) {
        throw std::out_of_range(fmt::format("unknown morphism {}", to_string(id)));
    }
    return morphisms_[*index];
}

MorphismId World::morphism_by_name(const std::string& name) const {
    const auto iter = morphism_names_.find(name);
    if (iter == morphism_names_.end()) {
        throw std::out_of_range(fmt::format("unknown morphism '{}'", name));
    }
    return iter->second;
}

bool World::has_morphism(const std::string& name) const noexcept {
    return morphism_names_.contains(name);
}

const std::vector<RelationPointer>& World::relations() const noexcept {
    return relations_;
}

const std::vector<Morphism>& World::morphisms() const noexcept {
    return morphisms_;
}

const std::vector<Region>& World::regions() const noexcept {
    return regions_;
}

const Region& World::region(RegionId id) const {
    const auto index = region_index(id);
    if (!index.has_value()) {
        throw std::out_of_range(fmt::format("unknown region {}", to_string(id)));
    }
    return regions_[*index];
}

const Region& World::region_by_name(const std::string& name) const {
    const auto iter = std::find_if(regions_.begin(), regions_.end(), [&name](const Region& region) {
        return region.name == name || to_string(region.id) == name;
    });
    if (iter == regions_.end()) {
        throw std::out_of_range(fmt::format("unknown region '{}'", name));
    }
    return *iter;
}

StructuralPressure World::pressure(ObjectHandle target) const {
    if (!contains(target)) {
        throw std::out_of_range(fmt::format("unknown object handle {}", to_string(target)));
    }
    return object_pressures_[target.slot];
}

std::string World::inspect_world() const {
    const auto current = snapshot();
    std::ostringstream output;
    output << fmt::format("objects: {}\n", current.object_count);
    output << fmt::format("pointers: {}\n", current.relation_count);
    output << fmt::format("morphisms: {}\n", current.morphism_count);
    output << fmt::format("laws: {}\n", current.law_count);
    output << fmt::format("regions: {}\n", current.region_count);
    output << fmt::format("trace length: {}\n", trace_.size());
    output << fmt::format("entropy: {:.6f}\n", current.graph_entropy);
    output << fmt::format("max pressure: {:.6f}\n", current.max_pressure);
    output << fmt::format("persistent constraints: {}\n", current.persistent_constraint_count);
    output << fmt::format("average region stability: {:.6f}", current.average_region_stability);
    return output.str();
}

std::string World::inspect_region(RegionId id) const {
    const auto& selected_region = region(id);
    std::ostringstream output;
    output << fmt::format("region: {}\n", selected_region.name);
    output << fmt::format("objects: {}\n", selected_region.objects.size());
    output << fmt::format("internal density: {:.6f}\n", selected_region.boundary.internal_density);
    output << fmt::format("boundary permeability: {:.6f}\n", selected_region.boundary.permeability);
    output << fmt::format("pressure: {:.6f}\n", selected_region.pressure.magnitude);
    output << fmt::format("persistence: {:.6f}\n", selected_region.pressure.persistence);
    output << fmt::format("stability: {:.6f}\n", selected_region.stability);
    output << fmt::format("origin: {}", selected_region.origin);
    return output.str();
}

std::string World::trace_origin(RegionId id) const {
    const auto& selected_region = region(id);
    std::ostringstream output;
    output << fmt::format("{} emerged from:\n", selected_region.name);
    output << fmt::format("  {}\n", selected_region.origin);
    output << fmt::format("  pressure={:.6f}\n", selected_region.pressure.magnitude);
    output << fmt::format("  persistence={:.6f}", selected_region.pressure.persistence);
    return output.str();
}

const std::vector<Law>& World::laws() const noexcept {
    return laws_;
}

const Trace& World::trace() const noexcept {
    return trace_;
}

std::uint64_t World::time() const noexcept {
    return time_;
}

std::optional<std::size_t> World::relation_index(RelationId id) const noexcept {
    const auto iter = std::find_if(relations_.begin(), relations_.end(), [id](const RelationPointer& relation) {
        return relation.id == id;
    });
    if (iter == relations_.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(relations_.begin(), iter));
}

std::optional<std::size_t> World::morphism_index(MorphismId id) const noexcept {
    const auto iter = std::find_if(morphisms_.begin(), morphisms_.end(), [id](const Morphism& morphism) {
        return morphism.id == id;
    });
    if (iter == morphisms_.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(morphisms_.begin(), iter));
}

std::optional<std::size_t> World::region_index(RegionId id) const noexcept {
    const auto iter = std::find_if(regions_.begin(), regions_.end(), [id](const Region& region) {
        return region.id == id;
    });
    if (iter == regions_.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(regions_.begin(), iter));
}

void World::apply_rewrite(std::vector<std::string>& events, std::vector<TraceEvent>& structured_events) {
    std::size_t rewritten_states = 0;
    std::size_t pressure_updates = 0;
    const auto base_phase = 0.013 * static_cast<double>(time_ + 1);

    for (auto& object : objects_) {
        if (object.state.empty()) {
            continue;
        }

        const auto relation_bias =
            0.001 * static_cast<double>(object.outgoing.size())
            + 0.0005 * static_cast<double>(object.incoming.size());

        auto& amplitudes = object.state.amplitudes();
        for (std::size_t index = 0; index < amplitudes.size(); ++index) {
            const auto phase = base_phase * static_cast<double>(index + 1) + relation_bias;
            amplitudes[index] *= std::polar(1.0, phase);
        }

        object.state.normalize();
        rewritten_states += 1;
    }

    for (const auto& relation : relations_) {
        if (!relation_is_pressure_source(relation)) {
            continue;
        }

        const auto magnitude = clamp01(std::abs(relation.weight));
        const auto pressure = compute_pressure(magnitude, estimate_graph_entropy(), 0.15 * magnitude, 0.25 * magnitude);
        add_pressure(relation.source, pressure);
        add_pressure(relation.target, pressure);
        pressure_updates += 2;

        structured_events.push_back(TraceEvent{
            time_ + 1,
            "pressure_update",
            {object(relation.source).name, object(relation.target).name},
            {to_string(relation.id)},
            {"unresolved_pressure"},
            {},
            {},
            pressure.magnitude,
            {{"law_residual", magnitude}},
            pressure.recurrence,
            fmt::format("relation '{}' generated structural pressure", relation.relation)
        });
    }

    events.push_back(fmt::format("rewrote {} state vector(s) by deterministic phase rotation", rewritten_states));
    if (pressure_updates > 0) {
        events.push_back(fmt::format("updated structural pressure on {} object endpoint(s)", pressure_updates));
    }
}

std::vector<LawResult> World::check_laws(const WorldSnapshot& before, const WorldSnapshot& after) const {
    std::vector<LawResult> results;
    results.reserve(laws_.size());
    for (const auto& law : laws_) {
        results.push_back(law.check(before, after));
    }
    return results;
}

double World::estimate_graph_entropy() const {
    if (objects_.empty()) {
        return 0.0;
    }

    const auto max_degree = std::max<std::size_t>(1, objects_.size() - 1);
    double entropy = 0.0;
    for (const auto& object : objects_) {
        const auto degree = object.incoming.size() + object.outgoing.size();
        entropy += static_cast<double>(degree) / static_cast<double>(max_degree);
    }
    return clamp01(entropy / static_cast<double>(objects_.size()));
}

double World::estimate_law_residual(const std::vector<LawResult>& law_results) const {
    double residual = 0.0;
    for (const auto& law : law_results) {
        residual = std::max(residual, law.passed ? 0.0 : std::max(1.0, std::abs(law.value)));
    }
    return clamp01(residual);
}

double World::estimate_state_drift(const WorldSnapshot& before, const WorldSnapshot& after) const {
    const auto probability_drift = std::abs(after.total_probability_mass - before.total_probability_mass);
    const auto pressure_drift = std::abs(after.total_pressure - before.total_pressure);
    const auto object_delta = after.object_count > before.object_count
        ? static_cast<double>(after.object_count - before.object_count) / std::max<double>(1.0, before.object_count)
        : 0.0;
    return clamp01(probability_drift + 0.5 * pressure_drift + object_delta);
}

double World::estimate_noncommutativity(const Morphism& morphism, const Object& object) const {
    const auto relation_factor = clamp01(static_cast<double>(object.incoming.size() + object.outgoing.size()) / 8.0);
    const auto effect_factor = (morphism.effect == "invert" || morphism.effect == "split") ? 0.35 : 0.10;
    return clamp01(relation_factor * effect_factor);
}

void World::add_pressure(ObjectHandle target, const StructuralPressure& pressure) {
    if (!contains(target) || target.slot >= object_pressures_.size()) {
        return;
    }

    auto& current = object_pressures_[target.slot];
    current.magnitude = clamp01(std::max(current.magnitude, pressure.magnitude));
    current.persistence += pressure.active(0.01) ? std::max(1.0, pressure.persistence) : 0.0;
    current.locality = clamp01(std::max(current.locality, pressure.locality));
    current.recurrence = clamp01(std::max(current.recurrence, pressure.recurrence));
    current.instability = clamp01(std::max(current.instability, pressure.instability));
}

std::size_t World::form_regions(std::vector<TraceEvent>& structured_events) {
    std::set<std::uint32_t> assigned;
    for (const auto& region : regions_) {
        for (const auto& object : region.objects) {
            assigned.insert(object.slot);
        }
    }

    std::vector<ObjectHandle> candidates;
    for (const auto& object : objects_) {
        if (assigned.contains(object.handle.slot)) {
            continue;
        }
        const auto& pressure = object_pressures_[object.handle.slot];
        if (pressure.magnitude >= 0.75 && pressure.persistence >= 3.0) {
            candidates.push_back(object.handle);
        }
    }

    if (candidates.size() < 3) {
        return 0;
    }

    const auto boundary = calculate_boundary(candidates);
    if (boundary.internal_density <= boundary.boundary_density) {
        return 0;
    }

    StructuralPressure average;
    for (const auto& handle : candidates) {
        const auto& pressure = object_pressures_[handle.slot];
        average.magnitude += pressure.magnitude;
        average.persistence += pressure.persistence;
        average.locality += pressure.locality;
        average.recurrence += pressure.recurrence;
        average.instability += pressure.instability;
    }
    const auto divisor = static_cast<double>(candidates.size());
    average.magnitude /= divisor;
    average.persistence /= divisor;
    average.locality /= divisor;
    average.recurrence /= divisor;
    average.instability /= divisor;

    if (average.magnitude < 0.75 || average.persistence < 3.0) {
        return 0;
    }

    Region region;
    region.id = RegionId{next_region_id_++};
    region.name = to_string(region.id);
    region.objects = candidates;
    region.boundary = boundary;
    region.pressure = average;
    region.stability = clamp01(1.0 - (average.magnitude * 0.65 + average.instability * 0.35));
    region.origin = "persistent unresolved pressure with stronger internal than boundary density";

    for (const auto& relation : relations_) {
        if (is_internal_relation(relation, candidates)) {
            region.internal_links.push_back(relation.id);
        }
    }

    const auto name = region.name;
    const auto pressure_delta = region.pressure.magnitude;
    regions_.push_back(std::move(region));
    structured_events.push_back(TraceEvent{
        time_,
        "region_formed",
        {},
        {},
        {"unresolved_pressure"},
        {},
        name,
        pressure_delta,
        {{"pressure", pressure_delta}},
        0.0,
        "region formed from persistent structural pressure"
    });

    return 1;
}

RegionBoundary World::calculate_boundary(const std::vector<ObjectHandle>& handles) const {
    RegionBoundary boundary;
    for (const auto& relation : relations_) {
        if (is_internal_relation(relation, handles)) {
            boundary.internal_links += 1;
        } else {
            const auto source_inside = std::find(handles.begin(), handles.end(), relation.source) != handles.end();
            const auto target_inside = std::find(handles.begin(), handles.end(), relation.target) != handles.end();
            if (source_inside || target_inside) {
                boundary.external_links += 1;
            }
        }
    }

    const auto n = handles.size();
    const auto max_internal = n > 1 ? n * (n - 1) : 1;
    boundary.internal_density = static_cast<double>(boundary.internal_links) / static_cast<double>(max_internal);
    boundary.boundary_density = static_cast<double>(boundary.external_links) / static_cast<double>(std::max<std::size_t>(1, n));
    boundary.permeability = clamp01(boundary.boundary_density / std::max(1e-9, boundary.internal_density + boundary.boundary_density));
    return boundary;
}

bool World::is_internal_relation(const RelationPointer& relation, const std::vector<ObjectHandle>& handles) const {
    return std::find(handles.begin(), handles.end(), relation.source) != handles.end()
        && std::find(handles.begin(), handles.end(), relation.target) != handles.end();
}

}  // namespace pointerverse
