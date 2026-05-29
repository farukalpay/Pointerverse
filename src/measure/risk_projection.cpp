// SPDX-License-Identifier: Apache-2.0
#include "pv/measure/risk_projection.hpp"

#include <limits>
#include <utility>

#include "pv/hash/hasher.hpp"
#include "pv/kernel/canonical_codec.hpp"

namespace pv {
namespace {

std::uint64_t saturating_mul(std::uint64_t left, std::uint64_t right) noexcept {
    if (left == 0 || right == 0) {
        return 0;
    }
    constexpr auto max = std::numeric_limits<std::uint64_t>::max();
    if (left > max / right) {
        return max;
    }
    return left * right;
}

std::uint64_t saturating_add(std::uint64_t left, std::uint64_t right) noexcept {
    constexpr auto max = std::numeric_limits<std::uint64_t>::max();
    if (max - left < right) {
        return max;
    }
    return left + right;
}

}  // namespace

std::uint64_t project(RiskVector value, RiskProjection projection) noexcept {
    std::uint64_t out = 0;
    out = saturating_add(out, saturating_mul(value.structural, projection.structural_weight));
    out = saturating_add(out, saturating_mul(value.law_distance, projection.law_weight));
    out = saturating_add(out, saturating_mul(value.repair_distance, projection.repair_weight));
    out = saturating_add(out, saturating_mul(value.surprise, projection.surprise_weight));
    return out;
}

Hash256 projection_policy_hash(RiskProjection projection) {
    CanonicalWriter writer;
    writer.string("ProjectionPolicy:v1");
    writer.u64(projection.structural_weight);
    writer.u64(projection.law_weight);
    writer.u64(projection.repair_weight);
    writer.u64(projection.surprise_weight);
    return sha256(writer.bytes());
}

Hash256 projection_result_hash(const ProjectionResult& result) {
    CanonicalWriter writer;
    writer.string("ProjectionResult:v1");
    writer.hash(result.measurement_hash);
    writer.hash(result.projection_policy_hash);
    writer.u64(result.projected_score);
    writer.string(result.decision);
    return sha256(writer.bytes());
}

ProjectionResult make_projection_result(
    Hash256 measurement_hash,
    RiskVector value,
    RiskProjection projection,
    std::string decision) {
    ProjectionResult result;
    result.measurement_hash = measurement_hash;
    result.projection_policy_hash = projection_policy_hash(projection);
    result.projected_score = project(value, projection);
    result.decision = std::move(decision);
    result.projection_hash = projection_result_hash(result);
    return result;
}

}  // namespace pv
