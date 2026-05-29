# Design and direction

Pointerverse is a deterministic engine for provable worlds. A world is a typed
graph; the only way it changes is a law-checked delta; every commit is
content-addressed; and any history can be replayed, forked, queried, explained,
and verified. This document records the guarantees the engine is built on, what
ships today, and where it can go next.

## Guarantees

These hold across every surface and are covered by the test suite.

- **Determinism.** The same script against the same state produces the same
  world hash, delta, execution plan, and proof.
- **Lawful transition.** A world only changes through a delta that the verifier
  admits. Strict mode rejects a violating transition; observe mode records it.
- **Content addressing.** Objects, deltas, snapshots, and proofs are addressed
  by hash, so a history is a Merkle DAG, and `fsck` revalidates the chain.
- **Replayable programs.** Each commit carries the program that produced it; the
  VM can re-execute it and confirm the stored delta and proof.
- **Projected observation.** Nothing reads or prints the world directly;
  observers produce measurable views.

## What ships today

- **Deterministic core** - stable handles, typed objects and relations, typed
  attributes on objects and pointers, snapshots, the `Delta:v2` operation
  algebra, and JSONL trace export.
- **Verifier and rules** - built-in laws plus a pattern rule DSL with `require`,
  temporal `require before` / `require after`, and `forbid` ("never after")
  constraints, loadable as reusable domain packages of schema and rules.
- **Category layer** - type-checked morphism composition tested at the level of
  world hash, law status, and observer projection.
- **Forkable repository** - branch, fork, run, query, saved query files,
  explanation of objects and commits (with exact delta summaries and per-law
  status), `why` for relations, branch comparison that names the first divergent
  commit on each side, history, and `fsck`.
- **Evidence ingestion** - an agent-log adapter for the `agent_audit` domain and
  a generic `graph-log` importer that turns any JSONL event stream into a
  forkable, verifiable world; idempotent by event id; observe and strict modes.
- **Audit** - reports, violations, timelines, risk scoring, JSON export, and
  `first-broke` to find the earliest commit a law broke on a branch.
- **Guard** - a one-command PR risk surface over git diffs, with text, Markdown,
  JSON, and SARIF reports and a composite GitHub Action.
- **Kernel hardening** - a derived fact store, world index, execution plans with
  read/write sets, canonical world roots, and `StoredCommit` proofs validated by
  `fsck`.
- **Sentinel** - staged boot, self-measurement, proof-chain and VM-replay
  patrols, and controlled fault injection.
- **Layered platform** - separate CMake targets for kernel, runtime, storage,
  Sentinel, query, rules, domains, ingest, Guard, Audit, and the CLI, with the
  layering enforced by architecture tests.

## Direction

Forward ideas, not missing pieces.

- Three-way merge that proposes a reconciled world, not only a conflict report.
- A query language over saved query files, with joins across branches.
- More importers: execution traces, OpenTelemetry spans, and CSV event tables.
- Optional remote stores so a `.pvstore` can be pushed, pulled, and shared.
- A richer Realms layer above the kernel that still commits typed deltas, passes
  the verifier, and emits trace events like every other subsystem.
