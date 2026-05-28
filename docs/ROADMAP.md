# Roadmap

## Phase 0 - Specification Lock

Define Object, Pointer, Morphism, Law, Observer, and Trace in documentation and
headers. Keep Realms as documentation only.

## Phase 1 - Deterministic Core

Implement stable handles, pointer graph, snapshots, deltas, commits, and JSONL
trace export. Same commands must produce the same world hash.

## Phase 2 - Law Verifier

Reject invalid transitions before commit. Initial laws are
`preserve_existing_identity`, `reject_dangling_pointer`, `preserve_relation_type`,
`bounded_weight`, and `no_invalid_epoch_reference`.

## Phase 3 - Category Layer

Make morphism composition type-checked and test identity/associativity at the
level of resulting world hash, law status, and observer projection.

## Phase 4 - Observer Layer

Ensure CLI output is produced through observer projections, not direct world
printing.

## Phase 5 - Realms Layer

Introduce symbolic pressure, recurrence, inner regions, and mythic continuity
only as a layer above lawful graph dynamics.
