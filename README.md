# Pointerverse

Pointerverse is a C++ PR guard that turns git diffs and agent/workflow evidence
into replayable, law-checked audit graphs.

The primary user-facing layer is **Pointerverse Guard**: a command-line PR risk
auditor for fast or AI-assisted code changes. It maps changed files into an
audit graph, runs concrete PR risk policies, persists the graph in `.pvstore`,
and emits PR-ready Markdown, JSON, SARIF, or text reports.

The underlying audit engine still models agents, tools, files, pull requests,
tests, secrets, repositories, and policies as typed objects and links. Repository
history can be replayed, queried, explained, forked, compared, and verified with
fsck.

## Build

Pointerverse uses C++23, CMake, Ninja, and vcpkg manifest mode.

```sh
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset default
cmake --build --preset default
ctest --preset default
```

To build only the kernel and CLI:

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
  -DPOINTERVERSE_BUILD_TESTS=OFF
cmake --build build
```

## Run

```sh
./build/pointerverse lab examples/minimal_reality.pv
./build/pointerverse repl
./build/pointerverse trace replay trace.jsonl
./build/pointerverse trace verify trace.jsonl --expect-hash 0x9f12a77103beaa20
./build/pointerverse repo init .pvstore
./build/pointerverse repo commit trace.jsonl
./build/pointerverse repo branch list
./build/pointerverse repo branch fork main experiment/a
./build/pointerverse repo branch compare main experiment/a
./build/pointerverse repo history main
./build/pointerverse repo run examples/agent_audit_valid.pv --branch main
./build/pointerverse repo query main objects type Agent
./build/pointerverse repo explain main object Agent0
./build/pointerverse repo why main Agent0 modifies FileA
./build/pointerverse repo fsck
./build/pointerverse ingest agent-log events.jsonl --domain agent_audit --branch main --mode observe
./build/pointerverse audit report main --format text
./build/pointerverse audit violations main
./build/pointerverse audit timeline main Agent0
./build/pointerverse audit export main --format json
./build/pointerverse guard run --repo . --base origin/main --mode observe
./build/pointerverse guard run --repo . --base origin/main --format markdown --out audit-report.md
```

## Pointerverse Guard

```sh
./build/pointerverse guard run --repo . --base main --mode observe
```

Default mode prints a text summary and writes these artifacts in the target
repository:

```txt
.pvstore/
audit-report.md
audit-report.json
audit.sarif
```

Use `--format` and `--out` to emit a single PR artifact:

```sh
./build/pointerverse guard run \
  --repo . \
  --base origin/main \
  --mode observe \
  --format markdown \
  --out audit-report.md
```

Current PR guard policies flag source changes without matching tests, possible
secret patterns in added lines, workflow changes, lockfile changes without
policy approval, generated output changes, large diffs, and deleted tests.

The directory-based demo works without creating a git repository:

```sh
./build/pointerverse guard run \
  --repo examples/pr_guard/after \
  --base ../before \
  --out report.md \
  --format markdown
```

## DSL sample

```txt
world new seed
object A : Node
object B : Node
link A -> B : causes weight=0.7
morphism Stabilize : Node -> Node
compose Stabilize after Stabilize
law add reject_dangling_pointer
law add bounded_weight
evolve 4
inspect graph
trace export trace.jsonl
```

## Audit DSL sample

```txt
domain use agent_audit

object Agent0 : Agent
object FileA : File

link Agent0 -> FileA : modifies weight=1.0

law add no_write_without_read
```

This is rejected because `Agent0` modifies `FileA` without a prior `reads`
relation. Custom pattern laws can be declared inline or loaded from a domain
rule file:

```txt
rule no_write_without_read
when link Agent -> File : modifies
require before link Agent -> File : reads
deny reason "{from} modifies {to} without prior read relation"
```

## Evidence ingest sample

```jsonl
{"id":"1","agent":"Agent0","event":"read_file","path":"src/main.cpp","ts":1710000000}
{"id":"2","agent":"Agent0","event":"write_file","path":"src/main.cpp","ts":1710000001}
{"id":"3","agent":"Agent0","event":"create_pr","pr":"PR42","ts":1710000002}
```

```sh
./build/pointerverse repo init .pvstore
./build/pointerverse ingest agent-log events.jsonl --branch main --mode observe
./build/pointerverse audit report main
```

`observe` mode records policy violations in accepted evidence commits. `strict`
mode rejects events whose graph transition violates active audit laws.

## License

Pointerverse is licensed under the Apache License, Version 2.0.
