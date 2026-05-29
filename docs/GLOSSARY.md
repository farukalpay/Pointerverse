# Glossary

`Object`: A typed entity in a world arena.

`Pointer`: A typed directed relation edge between objects.

`Morphism`: A typed world transformer that produces a delta.

`Delta`: A proposed transition payload. It is verified before commit.

`Law`: A measurement-backed invariant that can admit or reject a transition.

`Observer`: A projector that turns a snapshot into a measurable view.

`Trace`: A replayable JSONL event stream of committed and rejected transitions.

`Realm`: A world built and forked on the engine rather than a core primitive;
it ships today as the Mohács campaign demo pack.
