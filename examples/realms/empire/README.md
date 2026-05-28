# Pointerverse Realms: Empire Demo

This is a showcase layer above the Pointerverse kernel. It uses the same graph,
branching, law, proof, and explanation machinery as the repository and Guard
surfaces, but applies it to alternate-history pressure.

Run it from the repository root:

```sh
examples/realms/empire/run_demo.sh
```

Or run the flow manually:

```sh
pointerverse repo init examples/realms/empire/.empire
pointerverse repo --store examples/realms/empire/.empire run examples/realms/empire/main.pv --branch main
pointerverse repo --store examples/realms/empire/.empire branch fork main plague
pointerverse repo --store examples/realms/empire/.empire run examples/realms/empire/plague_branch.pv --branch plague
pointerverse repo --store examples/realms/empire/.empire why plague QuarantineEdict quarantines Harbor
```

The demo forks `plague`, `rebellion`, and `succession` branches from the same
kingdom state, applies different lawful histories, compares divergent branches,
and finishes with `repo fsck`.
