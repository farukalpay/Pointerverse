# Pointerverse

Pointerverse is a deterministic state kernel for content-addressed graph
worlds. It executes canonical instruction streams, applies typed deltas through
law checks, records commit proofs, and keeps enough structure to replay, query,
fork, compare, and verify repository history.

The same kernel can back several surfaces: a local graph lab, repository-backed
scripts, evidence ingestion, code review Guard reports, Realms demos, and now a
Sentinel runtime that continuously measures the store, proof chain, branch DAG,
and VM replay output.

```txt
DSL / Ingest / Guard / Apps -> Program -> Kernel VM -> Delta -> Proof -> Commit -> Sentinel
```

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
./build/pointerverse sentinel boot .pvstore
./build/pointerverse sentinel patrol .pvstore --once
./build/pointerverse sentinel report .pvstore
./build/pointerverse ingest agent-log events.jsonl --domain agent_audit --branch main --mode observe
./build/pointerverse audit report main --format text
./build/pointerverse audit violations main
./build/pointerverse audit timeline main Agent0
./build/pointerverse audit export main --format json
./build/pointerverse guard run --repo . --base origin/main --mode observe
./build/pointerverse guard run --repo . --base origin/main --format markdown --out audit-report.md
examples/kernel_stress/run_demo.sh
examples/sentinel_fault_demo/run_demo.sh
```

## Kernel Sentinel

Sentinel is the M9 self-verification runtime. It does not inspect the host OS or
try to police external processes. It measures Pointerverse’s own repository:
object blobs, branch refs, commit proofs, program objects, VM replay output, and
worker heartbeats.

```sh
./build/pointerverse sentinel boot .pvstore
./build/pointerverse sentinel patrol .pvstore --once
./build/pointerverse sentinel report .pvstore
```

The report is designed to make repository decay explicit:

```txt
Pointerverse Sentinel
---------------------
boot:              clean
regions checked:   31
objects checked:   42
commits checked:   8
program replays:   3
proof mismatches:  0
store corruptions: 0
worker heartbeats: clean
```

Controlled fault injection is available for demos and tests:

```sh
./build/pointerverse sentinel fault flip-proof .pvstore \
  --branch main \
  --commit HEAD \
  --yes-i-know-this-mutates-store
```

Then `sentinel boot` or `sentinel patrol` reports where the proof chain broke.
`examples/sentinel_fault_demo/run_demo.sh` runs the clean boot, injects a proof
fault, and verifies that Sentinel catches it.

## Guard Application

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

Guard maps code diffs into the same typed graph and emits review artifacts.
Current policies flag source changes without matching tests, possible secret
patterns in added lines, workflow changes, lockfile changes without policy
approval, generated output changes, large diffs, and deleted tests.

The directory-based demo works without creating a git repository:

```sh
./build/pointerverse guard run \
  --repo examples/pr_guard/after \
  --base ../before \
  --out report.md \
  --format markdown
```

### One-minute PR guard

Add this workflow to a repository to get a sticky PR comment, Check annotations,
SARIF Code Scanning upload, and downloadable audit artifacts:

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

The Action downloads the latest Linux x86_64 release binary when available and
falls back to a source build. It writes `audit-report.md`, `audit-report.json`,
`audit.sarif`, and a replayable `.pvstore/` audit graph.

### See it catch a risky PR

The fixture in `examples/pr_guard` intentionally changes source without matching
tests, adds a secret-like `.env` line, modifies a workflow, changes a lockfile,
touches generated output, and deletes a test. Pointerverse Guard reports:

```md
## Pointerverse Guard

Risk score: **100 / 100**
Status: **critical**

### Findings

- **HIGH**: .github/workflows/deploy.yml changes CI or deployment workflow state
- **CRITICAL**: possible secret introduced in config/dev.env (`config/dev.env:1`)
- **MEDIUM**: package-lock.json changed without policy approval
- **HIGH**: src/auth.cpp modified but no matching test file changed
- **MEDIUM**: src/generated/client.cpp appears to be generated or vendored output
- **HIGH**: tests/auth_test.cpp deleted from test coverage
```

On GitHub this same result appears as a PR comment and file annotations, so
reviewers do not need to open artifacts to see the risk.

## Realms demo pack

`examples/realms/empire` is a showcase layer above the kernel. It uses the same
graph, branch, law, query, and explanation machinery to fork an empire into
plague, rebellion, and succession histories.

```sh
examples/realms/empire/run_demo.sh
```

The demo runs `history`, `query`, `why`, `branch compare`, and `fsck` against
the resulting `.empire` store.

## Kernel stress demo

`examples/kernel_stress` exercises the M7 kernel directly: typed attributes,
operation batches, fact projection, indexes, execution plans, Merkle roots,
commit proofs, repository reopen, and fsck proof verification.

```sh
examples/kernel_stress/run_demo.sh
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
