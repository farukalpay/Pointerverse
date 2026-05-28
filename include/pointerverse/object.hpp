#pragma once

#include <map>
#include <string>
#include <vector>

#include "pointerverse/state_vector.hpp"
#include "pointerverse/types.hpp"

namespace pointerverse {

struct RelationPointer {
    RelationId id;
    ObjectHandle source;
    ObjectHandle target;
    std::string relation;
    double weight{1.0};
    CausalTag causality{CausalTag::Structural};
};

struct Object {
    ObjectHandle handle;
    std::string name;
    std::string type;
    std::map<std::string, std::string> config;
    StateVector state;
    std::vector<RelationId> outgoing;
    std::vector<RelationId> incoming;
};

}  // namespace pointerverse
