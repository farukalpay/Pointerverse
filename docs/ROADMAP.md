# Pointerverse Roadmap

Pointerverse is now oriented around **Pointerverse Audit**: replayable,
law-checked graph history for AI agents, workflows, and tool execution.

The Realms layer stays intentionally deferred until the audit engine has a
clear, useful domain surface.

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

## Phase 4 - Pointerverse Audit M4

Turn the persistent reality store into a domain-denied, queryable audit engine.
The first domain package is `agent_audit`, with object and relation vocabulary
for agents, tools, files, pull requests, test runs, repositories, secrets,
actions, and policies.

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

## Phase 5 - Evidence Ingestion & Audit Reports

Move Pointerverse Audit from hand-authored `.pv` scripts to real agent and
workflow evidence logs.

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

## Phase 6 - Realms Layer

Introduce symbolic pressure, recurrence, inner regions, and mythic continuity
only as a layer above lawful graph dynamics.

Status: deferred.

Do not start this phase until Pointerverse Audit has stronger query ergonomics,
more temporal law coverage, and real workflow/agent traces.

## Phase 7 - Audit Hardening

Next after M5:

- Add temporal rule operators for "first broke at commit" and "never after".
- Add saved query files and reusable domain packages.
- Make `repo explain` include exact delta summaries and law status diffs.
- Add branch divergence explanations that name the first causally relevant
  commit on each side.
- Add importers for real agent/tool execution traces.
