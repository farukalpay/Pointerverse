# Formal Model

Pointerverse M0 models a world as an epoch-indexed typed directed graph.

- `WorldId`, `Epoch`, and `ObjectId` qualify identity.
- `ObjectId` is a stable arena handle, not a raw memory address.
- `PointerEdge` is a typed relation with direction, causal role, weight,
  validity interval, and law domain.
- `Delta` is the only transition payload accepted by `World::commit`.
- `Verifier` evaluates a proposed transition against active laws before the
  world adopts the candidate state.
- `TraceRecorder` emits JSONL events for replay and analysis.

Object identity is local to `(WorldId, Epoch, ObjectId)`. Historical objects are
not removed from the arena; they change `ExistenceState`.
