# Pointerverse

Pointerverse is a verifiable graph-world engine. It runs deterministic
instruction streams over typed graph state, writes content-addressed commits,
and keeps proof material that can be replayed, queried, forked, compared, and
verified.

Guard, Sentinel, Realms, Audit, Repo, and World are product surfaces built on
top of the same engine.

```txt
World scripts / Evidence / Apps -> Kernel VM -> Runtime -> Store -> Sentinel
```

## Build

Pointerverse uses C++23, CMake, Ninja, and vcpkg manifest mode.

```sh
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset default
cmake --build --preset default
ctest --preset default
```

To build only the engine and CLI:

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
  -DPOINTERVERSE_BUILD_TESTS=OFF
cmake --build build
```

## First Run

The primary demo is a small city graph that forks into flood, blackout, and
evacuation histories, then asks Sentinel to verify the store.

```sh
./build/pointerverse world demo city
```

Equivalent direct commands:

```sh
./build/pointerverse repo init .pvstore
./build/pointerverse repo --store .pvstore run examples/packs/city/world.pv --branch main
./build/pointerverse repo --store .pvstore branch fork main blackout
./build/pointerverse repo --store .pvstore run examples/packs/city/blackout.pv --branch blackout
./build/pointerverse repo --store .pvstore query blackout objects type Infrastructure
./build/pointerverse repo --store .pvstore why blackout Hospital depends_on PowerPlant
./build/pointerverse sentinel boot .pvstore
```

## Surfaces

```sh
./build/pointerverse surfaces
./build/pointerverse surface show world
./build/pointerverse surface show guard
```

World builds and inspects graph worlds:

```sh
./build/pointerverse world run examples/packs/city/world.pv
./build/pointerverse world repl
```

Repo stores forkable histories:

```sh
./build/pointerverse repo init .pvstore
./build/pointerverse repo --store .pvstore branch list
./build/pointerverse repo --store .pvstore branch compare flood blackout
./build/pointerverse repo --store .pvstore fsck
```

Sentinel verifies store and proof-chain consistency:

```sh
./build/pointerverse sentinel boot .pvstore
./build/pointerverse sentinel patrol .pvstore --once
./build/pointerverse sentinel report .pvstore
./build/pointerverse pack run kernel_corruption
```

Guard is a practical code-review surface:

```sh
./build/pointerverse guard run \
  --repo examples/packs/code_review/after \
  --base ../before \
  --format markdown \
  --out audit-report.md
```

## Packs

Demo packs live under `examples/packs`.

```sh
./build/pointerverse packs
./build/pointerverse pack run city
./build/pointerverse pack run code_review
./build/pointerverse pack run empire
./build/pointerverse pack run kernel_corruption
```

Each pack has the same shape:

```txt
pack.toml
world.pv
run.sh
expected.txt
README.md
```

## Architecture

Pointerverse is split into layered CMake targets:

```txt
pointerverse_kernel
pointerverse_runtime
pointerverse_storage
pointerverse_sentinel
pointerverse_query
pointerverse_rules
pointerverse_domains
pointerverse_ingest
pointerverse_guard
pointerverse_audit
pointerverse_cli_common
pointerverse_sdk
```

The kernel does not include Guard, Audit, Ingest, Sentinel, Storage, or app
headers. Runtime does not include Storage or product surfaces. Storage does not
include Sentinel or product surfaces. The CLI registers surface commands as
separate modules.

## Guard Action

The GitHub Action wrapper remains available for repositories that want PR risk
reports, annotations, SARIF upload, and replayable `.pvstore/` artifacts.

```yaml
name: Pointerverse Guard

on:
  pull_request:

permissions:
  contents: read
  issues: write
  pull-requests: write
  security-events: write

jobs:
  guard:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          fetch-depth: 0

      - uses: farukalpay/Pointerverse@v0
        with:
          base: origin/main
          mode: observe
          upload-sarif: "true"
```
