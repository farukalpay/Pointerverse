# Glossary

`Object`: A typed entity in a world arena.

`Pointer`: A typed directed relation edge between objects.

`Morphism`: A typed world transformer that produces a delta.

`Delta`: A proposed transition payload. It is verified before commit.

`Law`: A measurement-backed invariant that can admit or reject a transition.

`Observer`: A projector that turns a snapshot into a measurable view.

`Trace`: A replayable JSONL event stream of committed and rejected transitions.

`InterventionOperator`: A canonical transformation descriptor used to replay a
branch history under a counterfactual change.

`InterventionProgram`: An ordered composition of intervention operators with a
canonical hash and cost.

`ScaleValue`: An exact dyadic rational scale used to refine intervention
families without floating-point ambiguity.

`Realm`: A world built and forked on the engine rather than a core primitive;
it ships today as the Mohács campaign demo pack.
