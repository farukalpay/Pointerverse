// SPDX-License-Identifier: Apache-2.0
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

#include <fmt/format.h>

#include "pv/query/query.hpp"
#include "pv/storage/integrity.hpp"
#include "pv/storage/repository.hpp"

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
    std::string profile{"quick"};
    std::size_t commits{256};
    std::size_t objects{128};
    std::size_t edges{128};
    std::size_t branches{4};
    std::uint64_t seed{1};
    bool cleanup{true};
};

std::uint64_t ms_since(Clock::time_point start) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
}

std::filesystem::path temp_repo_path(std::uint64_t seed) {
    return std::filesystem::temp_directory_path() / ("pointerverse_backend_stress_" + std::to_string(seed));
}

std::uint64_t store_size(const std::filesystem::path& root) {
    std::uint64_t size = 0;
    if (!std::filesystem::exists(root)) {
        return size;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file()) {
            size += static_cast<std::uint64_t>(entry.file_size());
        }
    }
    return size;
}

Options parse(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        auto require_value = [&](std::string_view name) -> std::string {
            if (index + 1 >= argc) {
                throw std::invalid_argument(fmt::format("{} requires a value", name));
            }
            return argv[++index];
        };
        if (arg == "--profile") {
            options.profile = require_value(arg);
            if (options.profile == "large") {
                options.commits = 10000;
                options.objects = 50000;
                options.edges = 100000;
                options.branches = 16;
            } else if (options.profile == "brutal") {
                options.commits = 100000;
                options.objects = 250000;
                options.edges = 1000000;
                options.branches = 100;
            } else if (options.profile != "quick") {
                throw std::invalid_argument("profile must be quick, large, or brutal");
            }
        } else if (arg == "--commits") {
            options.commits = std::stoull(require_value(arg));
        } else if (arg == "--objects") {
            options.objects = std::stoull(require_value(arg));
        } else if (arg == "--edges") {
            options.edges = std::stoull(require_value(arg));
        } else if (arg == "--branches") {
            options.branches = std::stoull(require_value(arg));
        } else if (arg == "--seed") {
            options.seed = std::stoull(require_value(arg));
        } else if (arg == "--no-cleanup") {
            options.cleanup = false;
        } else {
            throw std::invalid_argument("unknown option: " + arg);
        }
    }
    options.branches = std::max<std::size_t>(1, options.branches);
    return options;
}

pv::Transaction object_tx(pv::World& world, std::string name) {
    pv::Transaction tx;
    tx.label = "stress object";
    tx.delta = world.object_delta(std::move(name), "StressNode");
    return tx;
}

pv::Transaction edge_tx(pv::World& world, pv::ObjectId from, pv::ObjectId to) {
    pv::Transaction tx;
    tx.label = "stress edge";
    tx.delta = world.link_delta(from, to, "stress_link", 1.0, pv::CausalRole::Structural);
    return tx;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto options = parse(argc, argv);
        const auto root = temp_repo_path(options.seed);
        std::filesystem::remove_all(root);

        const auto start_total = Clock::now();
        auto repo = pv::Repository::init(root);
        (void)repo.create_branch("main", pv::World{"stress"});
        for (std::size_t branch = 1; branch < options.branches; ++branch) {
            (void)repo.fork("main", "branch/" + std::to_string(branch));
        }

        std::mt19937_64 rng{options.seed};
        const auto start_commits = Clock::now();
        std::size_t edge_commits = 0;
        for (std::size_t index = 0; index < options.commits; ++index) {
            const auto branch_index = index % options.branches;
            const auto branch = branch_index == 0 ? std::string{"main"} : "branch/" + std::to_string(branch_index);
            auto& world = repo.mutable_world(branch);
            const auto object_name = "O" + std::to_string(index);
            if (index < options.objects) {
                (void)repo.commit(branch, object_tx(world, object_name), pv::Verifier{});
            }
            const auto known_objects = std::min(options.objects, index + 1);
            if (known_objects > 1 && edge_commits < options.edges) {
                const auto from_name = "O" + std::to_string(rng() % known_objects);
                const auto to_name = "O" + std::to_string(rng() % known_objects);
                if (from_name != to_name && world.has_object(from_name) && world.has_object(to_name)) {
                    (void)repo.commit(
                        branch,
                        edge_tx(world, world.object_by_name(from_name), world.object_by_name(to_name)),
                        pv::Verifier{});
                    edge_commits += 1;
                }
            }
        }
        const auto commit_ms = ms_since(start_commits);

        const auto start_query = Clock::now();
        pv::RepositoryQueryEngine query;
        std::size_t query_hits = 0;
        for (std::size_t index = 0; index < 100; ++index) {
            query_hits += query.objects_by_type(repo, "main", "StressNode").objects.size();
        }
        const auto query_ms = ms_since(start_query);

        const auto start_materialize = Clock::now();
        (void)repo.world("main").snapshot();
        const auto materialize_ms = ms_since(start_materialize);

        const auto start_fsck = Clock::now();
        const auto fsck = pv::IntegrityChecker{}.check_repository(repo);
        const auto fsck_ms = ms_since(start_fsck);

        const auto start_compact = Clock::now();
        repo.compact();
        const auto compact_ms = ms_since(start_compact);

        const auto start_open = Clock::now();
        const auto reopened = pv::Repository::open(root);
        const auto cold_open_ms = ms_since(start_open);
        const auto stats = reopened.backend_stats();

        std::cout << "Backend stress\n";
        std::cout << "--------------\n";
        std::cout << fmt::format("profile:            {}\n", options.profile);
        std::cout << fmt::format("commits:            {}\n", stats.commits);
        std::cout << fmt::format("objects:            {}\n", stats.objects);
        std::cout << fmt::format("branches:           {}\n", stats.branches);
        std::cout << fmt::format("edges target:       {}\n", options.edges);
        std::cout << fmt::format("commit time:        {} ms\n", commit_ms);
        std::cout << fmt::format("cold open:          {} ms\n", cold_open_ms);
        std::cout << fmt::format("head materialize:   {} ms\n", materialize_ms);
        std::cout << fmt::format("query relation:     {} ms (hits {})\n", query_ms, query_hits);
        std::cout << fmt::format("fsck time:          {} ms\n", fsck_ms);
        std::cout << fmt::format("compact:            {} ms\n", compact_ms);
        std::cout << fmt::format("store size:         {} bytes\n", store_size(root));
        std::cout << fmt::format("final status:       {}\n", fsck.clean() ? "clean" : "dirty");
        std::cout << fmt::format("total time:         {} ms\n", ms_since(start_total));

        if (options.cleanup) {
            std::filesystem::remove_all(root);
        }
        return fsck.clean() ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& error) {
        std::cerr << fmt::format("error: {}\n", error.what());
        return EXIT_FAILURE;
    }
}
