# Pointerverse Roadmap

Pointerverse is oriented around a deterministic graph runtime: canonical
programs, law-checked deltas, commit proofs, replayable repository history, and
runtime self-verification.

Audit, Guard, ingestion, and Realms are applications of the same kernel. They
should make the core easier to understand, not redefine the project around a
single audience or workflow.

## Phase 0 - Specification Lock

Define Object, Pointer, Morphism, Law, Observer, and Trace in documentation and
headers. Keep Realms as documentation only.

Status: complete.

## Phase 1 - Deterministic Core

Implement stable handles, pointer graph, snapshots, deltas, commits, and JSONL
trace export. Same commands must produce the same world hash.

Status: complete.

## Phase 2 - Law Verifier

Reject invalid transitions before commit. Initial laws are
`preserve_existing_identity`, `reject_dangling_pointer`, `preserve_relation_type`,
`bounded_weight`, and `no_invalid_epoch_reference`.

Status: complete.

## Phase 3 - Category Layer

Make morphism composition type-checked and test identity/associativity at the
level of resulting world hash, law status, and observer projection.

Status: complete.

## Phase 4 - Domain Packages And Queries

Turn the persistent reality store into a domain-defined, queryable graph
history. The first domain package is `agent_audit`, with object and relation
vocabulary for agents, tools, files, pull requests, test runs, repositories,
secrets, actions, and policies.

Status: implemented.

Delivered:

- Domain package system with built-in `agent_audit`.
- Pattern-based rule engine and deterministic rule DSL.
- Hybrid rule loading from scripts and domain rule files.
- Repository-backed script execution with `repo run` and `repo repl`.
- Morphism application transactions with readable history labels.
- Graph/history query API and CLI query commands.
- Explanation commands for objects, commits, and relations.
- Agent audit examples and focused M4 tests.

Primary CLI surface:

```sh
pointerverse repo run examples/agent_audit_valid.pv --branch main
pointerverse repo query main objects type Agent
pointerverse repo query main links relation modifies
pointerverse repo query main commits touching object FileA
pointerverse repo query main cone object FileA depth 2 direction both
pointerverse repo explain main object Agent0
pointerverse repo explain main commit <hash>
pointerverse repo why main Agent0 modifies FileA
```

Audit law examples:

```txt
domain use agent_audit
law add no_write_without_read
law add no_pr_without_tests
law add no_secret_exposure
law add no_orphan_action
law add no_unapproved_external_write
```

Custom rule example:

```txt
rule no_write_without_read
when link Agent -> File : modifies
require before link Agent -> File : reads
deny reason "{from} modifies {to} without prior read relation"
```

## Phase 5 - Evidence Ingestion And Reports

Move from hand-authored `.pv` scripts to real event and evidence logs.

Status: implemented.

Delivered:

- Canonical evidence event and normalized audit event models.
- JSONL agent log adapter for tool, GitHub, CI, secret, and policy events.
- Idempotent ingestion index at `.pvstore/index/evidence`.
- Observe and strict ingestion modes.
- Temporal rule DSL support for `require before` and `require after`.
- Audit report, violation, timeline, risk score, and JSON export APIs.
- Top-level `ingest` and `audit` CLI commands.

Primary CLI surface:

```sh
pointerverse ingest agent-log events.jsonl --domain agent_audit --branch main --mode observe
pointerverse ingest agent-log events.jsonl --domain agent_audit --branch main --mode strict
pointerverse audit report main --format text
pointerverse audit report main --format json
pointerverse audit violations main
pointerverse audit timeline main Agent0
pointerverse audit export main --format json
```

Supported JSONL v1 examples:

```jsonl
{"id":"1","agent":"Agent0","event":"read_file","path":"src/main.cpp","ts":1710000000}
{"id":"2","agent":"Agent0","event":"write_file","path":"src/main.cpp","ts":1710000001}
{"id":"3","agent":"Agent0","event":"create_pr","pr":"PR42","ts":1710000002}
{"id":"4","event":"ci.test_passed","pr":"PR42","test":"Tests","ts":1710000003}
```

## Phase 6 - Guard Application

Add a one-command PR risk surface for real repositories without making code
review the only product direction.

Status: implemented.

Delivered:

- `pointerverse guard run` CLI for git refs and directory-demo diffs.
- Git diff adapter that maps name-status changes into evidence events.
- PR guard policy pack for missing tests, secret-like patterns, workflows,
  lockfiles, generated output, large diffs, and deleted tests.
- Text, Markdown, JSON, and SARIF guard reports.
- Default `.pvstore`, `audit-report.md`, `audit-report.json`, and `audit.sarif`
  artifacts.
- Composite GitHub Action wrapper and PR guard demo fixture.
- PR-facing Action visibility: step summary, sticky PR comment, GitHub
  annotations, SARIF upload option, and release binary download with source
  build fallback.

Primary CLI surface:

```sh
pointerverse guard run --repo . --base origin/main --mode observe
pointerverse guard run --repo . --base origin/main --format markdown --out audit-report.md
pointerverse guard run --repo . --base origin/main --markdown-out audit-report.md --json-out audit-report.json --sarif-out audit.sarif
pointerverse guard run --repo examples/pr_guard/after --base ../before --markdown-out audit-report.md --json-out audit-report.json --sarif-out audit.sarif
```

## Phase 7 - Kernel Hardening

Make Pointerverse a typed state kernel: operation batches over typed facts,
indexed graph storage, deterministic execution plans, canonical world roots,
and commit proofs.

Status: implemented.

Delivered:

- Typed `Value` and `Attribute` state on objects, pointers, and snapshots.
- `Delta:v2` operation algebra with v1 decode compatibility.
- Derived `FactStore` projection and canonical fact roots.
- `WorldIndex` for type, name, relation, edge, and attribute lookup.
- Execution plans with touched object/pointer sets and read/write fact sets.
- `StoredCommit:v2` commit proofs and fsck proof validation.
- `examples/kernel_stress` kernel stress demo.

The Realms empire pack remains available as a showcase layer above the kernel.

## Phase 8 - Kernel VM Program Chain

Make programs first-class commit material instead of decorative metadata.

Status: implemented.

Delivered:

- Canonical `Program` and `ProgramSymbolTable` objects.
- Instruction stream roots, symbol table hashes, and program hashes on commit
  records.
- `KernelVm::execute(snapshot, program)` as the real execution API.
- Program-bearing transactions with VM delta comparison before commit.
- Stored program objects and fsck replay checks for program/delta drift.

## Phase 9 - Kernel Sentinel

Add staged boot, self-measurement, heartbeat workers, proof-chain patrol, VM
replay patrol, and controlled fault injection.

Status: implemented.

Delivered:

- `Repository::open_with_sentinel()` for strict staged boot while preserving
  compatible `Repository::open()`.
- Boot stages for manifest, object store, branch refs, commit graph, latest
  snapshot, VM replay sample, proof chain, and ready.
- Boot measurement persisted at `.pvstore/sentinel/last_boot`.
- Integrity region table for Pointerverse-owned repository regions.
- Synchronous `StorePatrolWorker`, `ProofPatrolWorker`, and `VmReplayWorker`.
- `pointerverse sentinel boot`, `sentinel patrol`, `sentinel report`, and
  controlled `sentinel fault` commands.
- Fault demo at `examples/sentinel_fault_demo/run_demo.sh`.

## Future Work

- Add temporal rule operators for "first broke at commit" and "never after".
- Add saved query files and reusable domain packages.
- Make `repo explain` include exact delta summaries and law status diffs.
- Add branch divergence explanations that name the first causally relevant
  commit on each side.
- Add importers for broader execution traces and event streams.
