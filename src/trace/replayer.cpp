// SPDX-License-Identifier: Apache-2.0
#include "pv/trace/replayer.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace pv {
namespace {

struct ReplayGroup {
    Epoch epoch;
    Delta delta;
    std::size_t first_line{0};
    std::size_t state_events{0};
    std::size_t empty_markers{0};
    std::uint32_t next_temp{1};
    std::unordered_map<std::string, TempObjectId> created_objects;
};

bool is_state_event(std::string_view event) {
    return event == "object.create"
        || event == "object.update"
        || event == "pointer.create"
        || event == "pointer.remove";
}

bool is_empty_transition_marker(std::string_view event) {
    return event == "world.evolve"
        || event == "evolution.step"
        || event == "morphism.apply"
        || event == "morphism.compose";
}

bool is_metadata_event(std::string_view event) {
    return event == "law.check"
        || event == "world.transition.rejected"
        || event == "morphism.compose.rejected";
}

std::string field(const nlohmann::json& fields, std::string_view name, std::string fallback = {}) {
    if (!fields.is_object()) {
        return fallback;
    }
    const auto iter = fields.find(std::string{name});
    if (iter == fields.end() || !iter->is_string()) {
        return fallback;
    }
    return iter->get<std::string>();
}

double measurement(const nlohmann::json& measurements, std::string_view name, double fallback) {
    if (!measurements.is_object()) {
        return fallback;
    }
    const auto iter = measurements.find(std::string{name});
    if (iter == measurements.end() || !iter->is_number()) {
        return fallback;
    }
    return iter->get<double>();
}

PointerId pointer_id_from_string(const std::string& value) {
    if (value.size() < 2 || value.front() != 'P') {
        throw std::invalid_argument(fmt::format("invalid pointer id '{}'", value));
    }
    return PointerId{std::stoull(value.substr(1))};
}

void maybe_reset_world(World& world, const nlohmann::json& fields) {
    const auto world_name = field(fields, "world");
    if (!world_name.empty() && world.epoch().value == 0 && world.objects().empty() && world.name() != world_name) {
        world.reset(world_name);
    }
}

ObjectRef object_ref_for(World& world, ReplayGroup& group, const std::string& name) {
    if (const auto iter = group.created_objects.find(name); iter != group.created_objects.end()) {
        return ObjectRef{iter->second};
    }
    return ObjectRef{world.object_by_name(name)};
}

std::optional<ReplayError> append_state_event(
    World& world,
    ReplayGroup& group,
    const std::string& event,
    const nlohmann::json& fields,
    const nlohmann::json& measurements,
    std::size_t line) {
    try {
        if (event == "object.create") {
            const auto object_name = field(fields, "object");
            const auto type_name = field(fields, "type");
            if (object_name.empty() || type_name.empty()) {
                return ReplayError{line, event, "object.create requires object and type fields"};
            }
            const auto temp = TempObjectId{group.next_temp++};
            group.created_objects.emplace(object_name, temp);
            group.delta.creates.push_back(ObjectCreate{
                temp,
                object_name,
                world.type_id(type_name),
                existence_state_from_string(field(fields, "existence", "Alive"))
            });
            group.state_events += 1;
            return std::nullopt;
        }

        if (event == "object.update") {
            const auto object_name = field(fields, "object");
            if (object_name.empty()) {
                return ReplayError{line, event, "object.update requires object field"};
            }
            std::optional<TypeId> type;
            if (const auto type_name = field(fields, "type"); !type_name.empty()) {
                type = world.type_id(type_name);
            }
            std::optional<ExistenceState> existence;
            if (const auto existence_name = field(fields, "existence"); !existence_name.empty()) {
                existence = existence_state_from_string(existence_name);
            }
            group.delta.updates.push_back(ObjectUpdate{object_ref_for(world, group, object_name), type, existence});
            group.state_events += 1;
            return std::nullopt;
        }

        if (event == "pointer.create") {
            const auto from = field(fields, "from");
            const auto to = field(fields, "to");
            const auto relation = field(fields, "relation");
            if (from.empty() || to.empty() || relation.empty()) {
                return ReplayError{line, event, "pointer.create requires from, to, and relation fields"};
            }
            group.delta.links.push_back(PointerCreate{
                object_ref_for(world, group, from),
                object_ref_for(world, group, to),
                world.relation_type(relation),
                causal_role_from_string(field(fields, "role", "Structural")),
                Weight{measurement(measurements, "weight", 1.0)},
                field(fields, "law_domain", "core")
            });
            group.state_events += 1;
            return std::nullopt;
        }

        if (event == "pointer.remove") {
            group.delta.unlinks.push_back(PointerRemove{pointer_id_from_string(field(fields, "pointer"))});
            group.state_events += 1;
            return std::nullopt;
        }
    } catch (const std::exception& error) {
        return ReplayError{line, event, error.what()};
    }

    return ReplayError{line, event, "unsupported state event"};
}

void flush_group(std::optional<ReplayGroup>& group, ReplayResult& result, const Verifier& verifier) {
    if (!group.has_value()) {
        return;
    }

    const auto replayed_events = group->state_events == 0 && group->empty_markers > 0 ? 1 : group->state_events;
    if (replayed_events == 0) {
        group.reset();
        return;
    }

    const auto commit = result.world.commit(group->delta, verifier);
    if (!commit.accepted) {
        result.errors.push_back(ReplayError{
            group->first_line,
            "epoch",
            fmt::format("replay commit for epoch {} was rejected", group->epoch.value)
        });
    } else {
        result.events_replayed += replayed_events;
    }
    group.reset();
}

}  // namespace

ReplayResult TraceReplayer::replay_jsonl(std::string_view jsonl, const Verifier& verifier) const {
    ReplayResult result;

    std::istringstream input{std::string{jsonl}};
    std::string line;
    std::optional<ReplayGroup> group;
    std::size_t line_number = 0;

    while (std::getline(input, line)) {
        line_number += 1;
        if (line.empty()) {
            continue;
        }

        result.events_read += 1;

        nlohmann::json json;
        try {
            json = nlohmann::json::parse(line);
        } catch (const std::exception& error) {
            result.errors.push_back(ReplayError{line_number, {}, error.what()});
            continue;
        }

        const auto event = json.value("event", std::string{});
        const auto epoch = Epoch{json.value("epoch", std::uint64_t{0})};
        const auto fields = json.value("fields", nlohmann::json::object());
        const auto measurements = json.value("measurements", nlohmann::json::object());
        maybe_reset_world(result.world, fields);

        if (is_state_event(event) || is_empty_transition_marker(event)) {
            if (group.has_value() && group->epoch != epoch) {
                flush_group(group, result, verifier);
            }
            if (!group.has_value()) {
                ReplayGroup next_group;
                next_group.epoch = epoch;
                next_group.first_line = line_number;
                group = std::move(next_group);
            }

            if (is_empty_transition_marker(event)) {
                group->empty_markers += 1;
                continue;
            }

            if (auto error = append_state_event(result.world, *group, event, fields, measurements, line_number); error.has_value()) {
                result.errors.push_back(std::move(*error));
            }
            continue;
        }

        if (group.has_value() && group->epoch != epoch) {
            flush_group(group, result, verifier);
        }

        if (is_metadata_event(event)) {
            result.metadata_events += 1;
            continue;
        }

        result.errors.push_back(ReplayError{line_number, event, "unsupported event"});
    }

    flush_group(group, result, verifier);
    result.final_hash = result.world.hash();
    return result;
}

}  // namespace pv
