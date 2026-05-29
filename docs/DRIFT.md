# Design boundary

What makes a Pointerverse world provable is that everything in it is measured.
Narrative is welcome as a reading of a world; it never substitutes for the
mechanics that produce one. A symbolic or inner-world layer can be built on top
of the kernel, but it earns its place the way every layer does: by emitting
typed deltas, passing the verifier, and leaving trace events.

The boundary, stated as the engine keeps it:

- Laws and measurements decide transitions; lore names never stand in for them.
- The world changes only through a committed, verified delta, never by direct
  mutation.
- Observers read projections, not privileged internal state.
- A generated story is a view over committed facts, not behavior in itself.
- `include/pv/core` stays free of pressure, region, or continuity behavior;
  those belong to layers above it.

This is not a limit on imagination. It is what lets a world as large as a private
archive stay replayable, forkable, and verifiable.
