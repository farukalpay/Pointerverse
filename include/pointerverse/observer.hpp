#pragma once

#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include "pointerverse/types.hpp"

namespace pointerverse {

class World;

struct Observation {
    std::string observer_name;
    std::string target_name;
    std::string quantity;
    bool denied{false};
    std::string denial_reason;
    std::map<std::string, double> values;
    std::string summary;

    [[nodiscard]] nlohmann::json to_json() const;
};

class Observer {
public:
    explicit Observer(std::string name = "lab");
    Observer(
        std::string name,
        ObjectHandle position,
        std::size_t scope_radius,
        double resolution = 1.0,
        double bias = 0.0,
        std::size_t memory_depth = 16);

    [[nodiscard]] const std::string& name() const noexcept;
    [[nodiscard]] Observation observe(const World& world, ObjectHandle target, const std::string& quantity) const;
    [[nodiscard]] Observation observe_region(const World& world, const std::string& region_name, const std::string& quantity) const;
    [[nodiscard]] Observation observe_world(const World& world, const std::string& quantity) const;

private:
    [[nodiscard]] bool can_reach(const World& world, ObjectHandle target) const;
    [[nodiscard]] Observation denied(const std::string& target, const std::string& quantity, const std::string& reason) const;

    std::string name_;
    ObjectHandle position_{};
    std::size_t scope_radius_{64};
    double resolution_{1.0};
    double bias_{0.0};
    std::size_t memory_depth_{16};
};

}  // namespace pointerverse
