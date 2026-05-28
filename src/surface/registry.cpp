// SPDX-License-Identifier: Apache-2.0
#include "pv/surface/registry.hpp"

#include <algorithm>
#include <stdexcept>

#include <toml++/toml.hpp>

namespace pv {

std::vector<SurfaceManifest> built_in_surfaces() {
    return {
        SurfaceManifest{
            "world",
            "World",
            "Build and inspect verifiable graph worlds.",
            {
                "pointerverse world run examples/packs/city/world.pv",
                "pointerverse world repl",
                "pointerverse world demo city"
            },
            {
                "examples/packs/city",
                "examples/packs/supply_chain",
                "examples/packs/build_system"
            }
        },
        SurfaceManifest{
            "repo",
            "Repo",
            "Store forkable graph-world histories in a content-addressed repository.",
            {
                "pointerverse repo init .pvstore",
                "pointerverse repo --store .pvstore branch fork main blackout",
                "pointerverse repo --store .pvstore branch compare main blackout"
            },
            {"examples/packs/city"}
        },
        SurfaceManifest{
            "sentinel",
            "Sentinel",
            "Verify that a Pointerverse store still matches its proof chain.",
            {
                "pointerverse sentinel boot .pvstore",
                "pointerverse sentinel patrol .pvstore --once",
                "pointerverse sentinel fault flip-proof .pvstore --commit HEAD --yes-i-know-this-mutates-store"
            },
            {"examples/packs/kernel_corruption"}
        },
        SurfaceManifest{
            "guard",
            "Guard",
            "Turn code diffs into replayable graph evidence.",
            {
                "pointerverse guard run --repo . --base origin/main --mode observe",
                "pointerverse guard run --repo examples/packs/code_review/after --base ../before --format markdown"
            },
            {"examples/packs/code_review"}
        },
        SurfaceManifest{
            "realms",
            "Realms",
            "Fork and inspect symbolic worlds built on the same graph runtime.",
            {"pointerverse pack run empire"},
            {"examples/packs/empire"}
        },
        SurfaceManifest{
            "audit",
            "Audit",
            "Query normalized evidence as graph history.",
            {
                "pointerverse ingest agent-log events.jsonl --branch main --mode observe",
                "pointerverse audit report main --format text"
            },
            {"examples/packs/code_review"}
        }
    };
}

std::optional<SurfaceManifest> find_surface(std::string_view id) {
    const auto surfaces = built_in_surfaces();
    const auto found = std::ranges::find_if(surfaces, [&](const SurfaceManifest& surface) {
        return surface.id == id;
    });
    if (found == surfaces.end()) {
        return std::nullopt;
    }
    return *found;
}

PackManifest read_pack_manifest(const std::filesystem::path& pack_root) {
    const auto path = pack_root / "pack.toml";
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("missing pack manifest: " + path.string());
    }

    const auto table = toml::parse_file(path.string());
    PackManifest manifest;
    manifest.root = pack_root;
    manifest.id = table["id"].value_or("");
    manifest.title = table["title"].value_or("");
    manifest.surface = table["surface"].value_or("");
    manifest.entry = pack_root / table["entry"].value_or("world.pv");
    manifest.runner = pack_root / table["runner"].value_or("run.sh");
    manifest.expected = pack_root / table["expected"].value_or("expected.txt");
    manifest.readme = pack_root / table["readme"].value_or("README.md");
    if (manifest.id.empty() || manifest.title.empty() || manifest.surface.empty()) {
        throw std::runtime_error("pack manifest requires id, title, and surface: " + path.string());
    }
    return manifest;
}

std::vector<PackManifest> discover_packs(const std::filesystem::path& packs_root) {
    std::vector<PackManifest> packs;
    if (!std::filesystem::exists(packs_root)) {
        return packs;
    }
    for (const auto& entry : std::filesystem::directory_iterator(packs_root)) {
        if (!entry.is_directory()) {
            continue;
        }
        const auto manifest_path = entry.path() / "pack.toml";
        if (!std::filesystem::exists(manifest_path)) {
            continue;
        }
        packs.push_back(read_pack_manifest(entry.path()));
    }
    std::ranges::sort(packs, [](const PackManifest& left, const PackManifest& right) {
        return left.id < right.id;
    });
    return packs;
}

std::optional<PackManifest> find_pack(const std::filesystem::path& packs_root, std::string_view id) {
    const auto packs = discover_packs(packs_root);
    const auto found = std::ranges::find_if(packs, [&](const PackManifest& pack) {
        return pack.id == id;
    });
    if (found == packs.end()) {
        return std::nullopt;
    }
    return *found;
}

}  // namespace pv
