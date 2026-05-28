// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <filesystem>
#include <fmt/format.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "pv/core/world.hpp"
#include "pv/kernel/merkle.hpp"
#include "pv/runtime/transaction.hpp"
#include "pv/storage/integrity.hpp"
#include "pv/storage/repository.hpp"

namespace {

using namespace pv;

void require(bool condition, std::string message) {
    if (!condition) {
        throw std::runtime_error(std::move(message));
    }
}

Delta object_batch(World& world, std::size_t count) {
    Delta delta;
    for (std::size_t index = 0; index < count; ++index) {
        delta.append_create(ObjectCreate{
            TempObjectId{static_cast<std::uint32_t>(index + 1)},
            fmt::format("Node{:03}", index),
            world.type_id(index % 3 == 0 ? "ComputeShard" : "FactNode"),
            ExistenceState::Alive,
            {
                Attribute{"slot", uint64_value(index)},
                Attribute{"actor", string_value(index % 2 == 0 ? "planner" : "executor")},
                Attribute{"risk", float64_value(static_cast<double>(index % 100) / 100.0)}
            }
        });
    }
    return delta;
}

Delta link_batch(World& world, std::size_t object_count, std::size_t offset, std::size_t count) {
    Delta delta;
    for (std::size_t index = 0; index < count; ++index) {
        const auto edge = offset + index;
        const auto from = world.object_by_name(fmt::format("Node{:03}", edge % object_count));
        const auto to = world.object_by_name(fmt::format("Node{:03}", (edge * 17 + 11) % object_count));
        delta.append_link(PointerCreate{
            ObjectRef{from},
            ObjectRef{to},
            world.relation_type(edge % 2 == 0 ? "depends_on" : "observes"),
            edge % 2 == 0 ? CausalRole::Structural : CausalRole::Observational,
            Weight{static_cast<double>((edge % 91) + 1) / 100.0},
            "kernel_stress",
            {
                Attribute{"batch", uint64_value(offset / count)},
                Attribute{"edge", uint64_value(edge)}
            }
        });
    }
    return delta;
}

CommitRecord commit_or_throw(Repository& repo, Transaction tx) {
    auto record = repo.commit("main", std::move(tx), Verifier{});
    require(record.has_value(), "repository refused to create commit");
    require(record->accepted, "kernel stress commit rejected");
    return *record;
}

void populate(Repository& repo) {
    (void)repo.create_branch("main", World{"kernel-stress"});
    auto& world = repo.mutable_world("main");

    Transaction create;
    create.label = "kernel stress create typed objects";
    create.delta = object_batch(world, 240);
    commit_or_throw(repo, std::move(create));

    for (std::size_t offset = 0; offset < 1200; offset += 200) {
        Transaction links;
        links.label = fmt::format("kernel stress link batch {}", offset / 200);
        links.delta = link_batch(repo.mutable_world("main"), 240, offset, 200);
        commit_or_throw(repo, std::move(links));
    }
}

Hash256 build_repo_and_root(const std::filesystem::path& path) {
    std::filesystem::remove_all(path);
    auto repo = Repository::init(path);
    populate(repo);
    const auto root = compute_world_root(repo.world("main").snapshot()).root;
    const auto report = IntegrityChecker{}.check_repository(repo);
    require(report.clean(), "fresh repository failed fsck");
    return root;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto root = argc > 1
            ? std::filesystem::path{argv[1]}
            : std::filesystem::path{"examples/kernel_stress/.kernel_stress"};
        const auto mirror = root.string() + ".mirror";

        const auto first_root = build_repo_and_root(root);
        const auto second_root = build_repo_and_root(mirror);
        require(first_root == second_root, "same seed and operation batches produced different roots");

        auto reopened = Repository::open(root);
        const auto snapshot = reopened.world("main").snapshot();
        auto shuffled = snapshot;
        std::ranges::reverse(shuffled.objects);
        std::ranges::reverse(shuffled.pointers);
        std::ranges::reverse(shuffled.facts);
        require(compute_world_root(snapshot).root == compute_world_root(shuffled).root, "snapshot insertion order changed root");

        auto tampered_attribute = snapshot;
        tampered_attribute.objects.front().attributes.push_back(Attribute{"tampered", bool_value(true)});
        require(compute_world_root(snapshot).root != compute_world_root(tampered_attribute).root, "tampered attribute was not detected");

        auto tampered_pointer = snapshot;
        tampered_pointer.pointers.front().weight = Weight{0.123456};
        require(compute_world_root(snapshot).root != compute_world_root(tampered_pointer).root, "tampered pointer was not detected");

        Transaction tx;
        tx.label = "kernel stress plan read write sample";
        tx.delta.append_set_object_attribute(
            ObjectRef{reopened.world("main").object_by_name("Node000")},
            Attribute{"verified", bool_value(true)});
        const auto prepared = prepare_transaction(reopened.world("main"), tx, Verifier{});
        require(prepared.committable, "sample transaction did not prepare");
        require(prepared.execution_plan.resolved_ops.size() == 1, "sample plan did not resolve exactly one op");
        require(prepared.execution_plan.resolved_ops.front().touched_objects.size() == 1, "sample plan touched unexpected objects");
        require(prepared.execution_plan.resolved_ops.front().touched_pointers.empty(), "sample plan touched unexpected pointers");
        require(!prepared.execution_plan.resolved_ops.front().reads.empty(), "sample plan has empty read set");
        require(!prepared.execution_plan.resolved_ops.front().writes.empty(), "sample plan has empty write set");

        auto proof = *prepared.proof;
        proof.after_root = first_root;
        require(hash_commit_proof(proof) != hash_commit_proof(*prepared.proof), "tampered proof hash did not change");

        const auto report = IntegrityChecker{}.check_repository(reopened);
        require(report.clean(), "reopened repository failed fsck");

        std::cout << "Pointerverse M7 kernel stress\n";
        std::cout << fmt::format("objects: {}\n", snapshot.objects.size());
        std::cout << fmt::format("pointers: {}\n", snapshot.pointers.size());
        std::cout << fmt::format("facts: {}\n", snapshot.facts.size());
        std::cout << fmt::format("world root: {}\n", to_hex(first_root));
        std::cout << "status: verified\n";

        std::filesystem::remove_all(mirror);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "kernel stress failed: " << error.what() << '\n';
        return 1;
    }
}
