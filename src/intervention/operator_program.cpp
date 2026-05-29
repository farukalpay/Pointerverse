// SPDX-License-Identifier: Apache-2.0
#include "pv/intervention/operator_program.hpp"

#include <algorithm>
#include <numeric>
#include <utility>

#include "pv/hash/hasher.hpp"
#include "pv/kernel/canonical_codec.hpp"

namespace pv {

void encode(CanonicalWriter& writer, const InterventionProgram& program) {
    writer.string("InterventionProgram:v1");
    writer.u64(program.operators.size());
    for (const auto& op : program.operators) {
        writer.hash(op.canonical_hash);
    }
    writer.u64(program.canonical_cost);
}

Hash256 intervention_program_hash(const InterventionProgram& program) {
    CanonicalWriter writer;
    encode(writer, program);
    return sha256(writer.bytes());
}

InterventionProgram identity_intervention_program() {
    InterventionProgram program;
    program.canonical_hash = intervention_program_hash(program);
    return program;
}

InterventionProgram canonicalize_intervention_program(InterventionProgram program) {
    std::uint64_t cost = 0;
    for (auto& op : program.operators) {
        op = canonicalize_intervention_operator(std::move(op));
        cost += op.canonical_cost;
    }
    program.canonical_cost = cost;
    program.canonical_hash = intervention_program_hash(program);
    return program;
}

InterventionProgram make_intervention_program(std::vector<InterventionOperator> operators) {
    InterventionProgram program;
    program.operators = std::move(operators);
    return canonicalize_intervention_program(std::move(program));
}

std::string intervention_program_id(const InterventionProgram& program) {
    return to_hex(program.canonical_hash).substr(0, 12);
}

bool equivalent_program(const InterventionProgram& left, const InterventionProgram& right) noexcept {
    return left.canonical_hash == right.canonical_hash;
}

}  // namespace pv
