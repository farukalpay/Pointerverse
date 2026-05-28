// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "pv/core/delta.hpp"
#include "pv/core/snapshot.hpp"
#include "pv/law/verifier.hpp"
#include "pv/trace/recorder.hpp"

namespace pv {

struct CommitResult {
    bool accepted{false};
    Epoch before_epoch;
    Epoch after_epoch;
    std::vector<LawStatus> law_statuses;
    std::vector<LawViolation> violations;
    std::vector<TraceEvent> events;
    std::uint64_t world_hash{0};
};

struct EvolveResult {
    std::size_t requested_steps{0};
    std::size_t completed_steps{0};
    std::size_t rejected_steps{0};
    std::vector<LawStatus> last_law_statuses;
};

class World {
public:
    explicit World(std::string name = "world", WorldId id = WorldId{1});

    void reset(std::string name, WorldId id = WorldId{1});

    [[nodiscard]] WorldId id() const noexcept;
    [[nodiscard]] Epoch epoch() const noexcept;
    [[nodiscard]] const std::string& name() const noexcept;

    [[nodiscard]] TypeId type_id(std::string_view name);
    [[nodiscard]] RelationType relation_type(std::string_view name);
    [[nodiscard]] std::string type_name(TypeId type) const;
    [[nodiscard]] std::string relation_name(RelationType relation) const;

    [[nodiscard]] Delta object_delta(std::string name, std::string_view type);
    [[nodiscard]] Delta link_delta(ObjectId from, ObjectId to, std::string_view relation, double weight, CausalRole role);
    [[nodiscard]] Delta existence_delta(ObjectId object, ExistenceState state);

    [[nodiscard]] CommitResult commit(const Delta& delta, const Verifier& verifier);
    [[nodiscard]] EvolveResult evolve(std::size_t steps, const Verifier& verifier);
    [[nodiscard]] WorldSnapshot snapshot() const;

    [[nodiscard]] bool contains(ObjectId id) const noexcept;
    [[nodiscard]] bool has_object(std::string_view name) const;
    [[nodiscard]] ObjectId object_by_name(std::string_view name) const;
    [[nodiscard]] const Object& object(ObjectId id) const;
    [[nodiscard]] const PointerEdge& pointer(PointerId id) const;
    [[nodiscard]] const std::vector<Object>& objects() const noexcept;
    [[nodiscard]] const std::vector<PointerEdge>& pointers() const noexcept;
    [[nodiscard]] const TraceRecorder& trace() const noexcept;
    [[nodiscard]] std::uint64_t hash() const;

private:
    [[nodiscard]] std::vector<TraceEvent> apply_delta_unchecked(const Delta& delta);
    [[nodiscard]] std::optional<std::size_t> pointer_index(PointerId id) const noexcept;
    void append_rejection_trace(const Delta& delta, const std::string& reason, const std::vector<LawViolation>& violations);

    WorldId id_;
    std::string name_;
    Epoch epoch_;
    TypeRegistry types_;
    RelationRegistry relations_;
    ObjectArena objects_;
    std::unordered_map<std::string, ObjectId> object_names_;
    std::vector<PointerEdge> pointers_;
    TraceRecorder trace_;
    std::uint64_t next_pointer_id_{1};
};

}  // namespace pv
