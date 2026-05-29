// SPDX-License-Identifier: Apache-2.0
#include "pv/observer/observer.hpp"

#include <fmt/format.h>
#include <sstream>

namespace pv {

Observer::Observer(std::string name) : name_(std::move(name)) {}

Projection Observer::inspect_object(const WorldSnapshot& snapshot, ObjectId id) const {
    Projection projection;
    projection.observer = name_;
    projection.target = to_string(id);

    const auto* object = snapshot.object(id);
    if (object == nullptr) {
        projection.body = fmt::format("object {}: unavailable", to_string(id));
        projection.measurements.emplace("exists", 0.0);
        return projection;
    }

    projection.target = object->name;
    projection.measurements.emplace("incoming", static_cast<double>(object->incoming_count));
    projection.measurements.emplace("outgoing", static_cast<double>(object->outgoing_count));

    std::ostringstream body;
    body << fmt::format(
        "object {}\n  id: {}\n  type: {}\n  existence: {}\n  incoming: {}\n  outgoing: {}",
        object->name,
        to_string(object->id),
        snapshot.type_name(object->type),
        to_string(object->existence),
        object->incoming_count,
        object->outgoing_count);
    if (!object->attributes.empty()) {
        body << "\n  attributes:";
        for (const auto& attribute : object->attributes) {
            body << fmt::format("\n    {} = {}", attribute.key, to_string(attribute.value));
        }
    }
    projection.body = body.str();
    return projection;
}

Projection Observer::inspect_object(const WorldSnapshot& snapshot, std::string_view name) const {
    for (const auto& object : snapshot.objects) {
        if (object.name == name) {
            return inspect_object(snapshot, object.id);
        }
    }

    Projection projection;
    projection.observer = name_;
    projection.target = std::string{name};
    projection.body = fmt::format("object {}: unavailable", name);
    projection.measurements.emplace("exists", 0.0);
    return projection;
}

Projection Observer::inspect_graph(const WorldSnapshot& snapshot) const {
    Projection projection;
    projection.observer = name_;
    projection.target = snapshot.world_name;
    projection.measurements.emplace("objects", static_cast<double>(snapshot.objects.size()));
    projection.measurements.emplace("pointers", static_cast<double>(snapshot.pointers.size()));

    std::ostringstream output;
    output << fmt::format("World({}) epoch={}\n\n", snapshot.world_name, snapshot.epoch.value);
    output << fmt::format("objects: {}\n", snapshot.objects.size());
    output << fmt::format("pointers: {}\n", snapshot.pointers.size());

    if (!snapshot.pointers.empty()) {
        output << "\nrelations:\n";
        for (const auto& pointer : snapshot.pointers) {
            const auto* from = snapshot.object(pointer.from);
            const auto* to = snapshot.object(pointer.to);
            output << fmt::format(
                "  {} -> {} : {} role={} weight={:.6g}\n",
                from != nullptr ? from->name : to_string(pointer.from),
                to != nullptr ? to->name : to_string(pointer.to),
                snapshot.relation_name(pointer.relation),
                to_string(pointer.causal_role),
                pointer.weight.value);
        }
    }

    projection.body = output.str();
    return projection;
}

}  // namespace pv
