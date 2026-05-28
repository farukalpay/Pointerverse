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
require exists link Agent -> File : reads
deny reason "{from} modifies {to} without prior read relation"
```

## Phase 5 - Realms Layer

Introduce symbolic pressure, recurrence, inner regions, and mythic continuity
only as a layer above lawful graph dynamics.

Status: deferred.

Do not start this phase until Pointerverse Audit has stronger query ergonomics,
more temporal law coverage, and real workflow/agent traces.

## Phase 6 - Audit Hardening

Next after M4:

- Add temporal rule operators for "exists before current epoch", "first broke
  at commit", and "never after".
- Add saved query files and reusable domain packages.
- Make `repo explain` include exact delta summaries and law status diffs.
- Add branch divergence explanations that name the first causally relevant
  commit on each side.
- Add importers for real agent/tool execution traces.
