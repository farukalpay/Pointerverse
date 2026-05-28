# Empire Pack

Fork a symbolic empire into plague, rebellion, and succession histories using
the same repository, branch, law, query, and explanation machinery as the other
Pointerverse surfaces.

```sh
pointerverse pack run empire
```

The direct flow is:

```sh
pointerverse repo init examples/packs/empire/.pack-store
pointerverse repo --store examples/packs/empire/.pack-store run examples/packs/empire/world.pv --branch main
pointerverse repo --store examples/packs/empire/.pack-store branch fork main plague
pointerverse repo --store examples/packs/empire/.pack-store run examples/packs/empire/plague_branch.pv --branch plague
pointerverse repo --store examples/packs/empire/.pack-store why plague QuarantineEdict quarantines Harbor
```
