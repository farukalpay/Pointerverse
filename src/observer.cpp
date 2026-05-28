#include "pointerverse/observer.hpp"

#include <fmt/format.h>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>

#include "pointerverse/world.hpp"

namespace pointerverse {

nlohmann::json Observation::to_json() const {
    return {
        {"observer_name", observer_name},
        {"target_name", target_name},
        {"quantity", quantity},
        {"denied", denied},
        {"denial_reason", denial_reason},
        {"values", values},
        {"summary", summary}
    };
}

Observer::Observer(std::string name) : name_(std::move(name)) {}

Observer::Observer(
    std::string name,
    ObjectHandle position,
    std::size_t scope_radius,
    double resolution,
    double bias,
    std::size_t memory_depth)
    : name_(std::move(name)),
      position_(position),
      scope_radius_(scope_radius),
      resolution_(resolution),
      bias_(bias),
      memory_depth_(memory_depth) {}

const std::string& Observer::name() const noexcept {
    return name_;
}

Observation Observer::observe(const World& world, ObjectHandle target, const std::string& quantity) const {
    const auto& object = world.object(target);

    if (!can_reach(world, target)) {
        return denied(object.name, quantity, "observer scope too narrow");
    }

    Observation observation;
    observation.observer_name = name_;
    observation.target_name = object.name;
    observation.quantity = quantity;

    if (quantity == "probabilities") {
        const auto probabilities = object.state.probabilities();
        std::ostringstream summary;
        for (std::size_t index = 0; index < probabilities.size(); ++index) {
            const auto key = fmt::format("p{}", index);
            observation.values.emplace(key, probabilities[index]);
            if (index != 0) {
                summary << ", ";
            }
            summary << fmt::format("probability[{}]={:.6f}", index, probabilities[index]);
        }
        observation.summary = summary.str();
        return observation;
    }

    if (quantity == "norm") {
        const auto norm = object.state.norm();
        observation.values.emplace("norm", norm);
        observation.summary = fmt::format("norm={:.12f}", norm);
        return observation;
    }

    if (quantity == "entropy") {
        const auto entropy = object.state.entropy();
        observation.values.emplace("entropy", entropy * resolution_ + bias_);
        observation.summary = fmt::format("entropy={:.12f}", entropy * resolution_ + bias_);
        return observation;
    }

    if (quantity == "pressure") {
        const auto pressure = world.pressure(target);
        observation.values.emplace("magnitude", pressure.magnitude);
        observation.values.emplace("persistence", pressure.persistence);
        observation.values.emplace("instability", pressure.instability);
        observation.summary = fmt::format(
            "pressure={:.6f} persistence={:.6f} instability={:.6f}",
            pressure.magnitude,
            pressure.persistence,
            pressure.instability);
        return observation;
    }

    if (quantity == "ancestry") {
        std::ostringstream summary;
        summary << object.name;
        std::size_t count = 0;
        for (const auto& relation_id : object.incoming) {
            if (count >= memory_depth_) {
                break;
            }
            const auto& relation = world.relation(relation_id);
            summary << " <- " << world.object(relation.source).name;
            count += 1;
        }
        observation.values.emplace("depth", static_cast<double>(count));
        observation.summary = summary.str();
        return observation;
    }

    if (quantity == "state") {
        observation.values.emplace("dimension", static_cast<double>(object.state.dimension()));
        observation.values.emplace("norm", object.state.norm());
        observation.summary = object.state.summary();
        return observation;
    }

    throw std::invalid_argument(fmt::format("unknown observation quantity '{}'", quantity));
}

Observation Observer::observe_region(const World& world, const std::string& region_name, const std::string& quantity) const {
    const auto& selected_region = world.region_by_name(region_name);
    bool reachable = !position_.is_valid_token();
    for (const auto& object : selected_region.objects) {
        reachable = reachable || can_reach(world, object);
    }
    if (!reachable) {
        return denied(region_name, quantity, "observer scope too narrow");
    }

    Observation observation;
    observation.observer_name = name_;
    observation.target_name = selected_region.name;
    observation.quantity = quantity;

    if (quantity == "pressure") {
        observation.values.emplace("magnitude", selected_region.pressure.magnitude);
        observation.values.emplace("persistence", selected_region.pressure.persistence);
        observation.values.emplace("instability", selected_region.pressure.instability);
        observation.summary = fmt::format("pressure={:.6f}", selected_region.pressure.magnitude);
        return observation;
    }

    if (quantity == "stability") {
        observation.values.emplace("stability", selected_region.stability);
        observation.summary = fmt::format("stability={:.6f}", selected_region.stability);
        return observation;
    }

    if (quantity == "boundary") {
        observation.values.emplace("internal_density", selected_region.boundary.internal_density);
        observation.values.emplace("permeability", selected_region.boundary.permeability);
        observation.summary = fmt::format(
            "internal_density={:.6f} permeability={:.6f}",
            selected_region.boundary.internal_density,
            selected_region.boundary.permeability);
        return observation;
    }

    throw std::invalid_argument(fmt::format("unknown region observation quantity '{}'", quantity));
}

Observation Observer::observe_world(const World& world, const std::string& quantity) const {
    if (position_.is_valid_token()) {
        return denied("world", quantity, "observer scope too narrow");
    }

    const auto snapshot = world.snapshot();
    Observation observation;
    observation.observer_name = name_;
    observation.target_name = "world";
    observation.quantity = quantity;

    if (quantity == "entropy") {
        observation.values.emplace("entropy", snapshot.graph_entropy);
        observation.summary = fmt::format("entropy={:.6f}", snapshot.graph_entropy);
        return observation;
    }

    if (quantity == "pressure") {
        observation.values.emplace("max_pressure", snapshot.max_pressure);
        observation.values.emplace("total_pressure", snapshot.total_pressure);
        observation.summary = fmt::format(
            "max_pressure={:.6f} total_pressure={:.6f}",
            snapshot.max_pressure,
            snapshot.total_pressure);
        return observation;
    }

    throw std::invalid_argument(fmt::format("unknown world observation quantity '{}'", quantity));
}

bool Observer::can_reach(const World& world, ObjectHandle target) const {
    if (!position_.is_valid_token()) {
        return true;
    }
    if (!world.contains(position_) || !world.contains(target)) {
        return false;
    }
    if (position_ == target) {
        return true;
    }
    if (scope_radius_ == 0) {
        return false;
    }

    std::queue<std::pair<ObjectHandle, std::size_t>> queue;
    std::set<std::uint32_t> seen;
    queue.emplace(position_, 0);
    seen.insert(position_.slot);

    while (!queue.empty()) {
        const auto [current, depth] = queue.front();
        queue.pop();
        if (depth >= scope_radius_) {
            continue;
        }

        const auto& object = world.object(current);
        for (const auto relation_id : object.outgoing) {
            const auto& relation = world.relation(relation_id);
            if (relation.target == target) {
                return true;
            }
            if (!seen.contains(relation.target.slot)) {
                seen.insert(relation.target.slot);
                queue.emplace(relation.target, depth + 1);
            }
        }
        for (const auto relation_id : object.incoming) {
            const auto& relation = world.relation(relation_id);
            if (relation.source == target) {
                return true;
            }
            if (!seen.contains(relation.source.slot)) {
                seen.insert(relation.source.slot);
                queue.emplace(relation.source, depth + 1);
            }
        }
    }

    return false;
}

Observation Observer::denied(const std::string& target, const std::string& quantity, const std::string& reason) const {
    Observation observation;
    observation.observer_name = name_;
    observation.target_name = target;
    observation.quantity = quantity;
    observation.denied = true;
    observation.denial_reason = reason;
    observation.summary = fmt::format("denied: {}", reason);
    return observation;
}

}  // namespace pointerverse
