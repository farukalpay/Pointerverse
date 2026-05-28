# Pointerverse

Pointerverse is a C++ experimental reality kernel where objects exist through
typed relations, transformations compose as morphisms, and every world
transition is admitted or rejected by explicit laws.

It is not a game engine, not a story generator, and not an AI chatbot. It is a
lawful graph-based substrate for building artificial realities whose internal
structure can be measured, replayed, forked, and analyzed.

## Build

Pointerverse uses C++23, CMake, Ninja, and vcpkg manifest mode.

```sh
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset default
cmake --build --preset default
ctest --preset default
```

To build only the kernel and CLI:

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
  -DPOINTERVERSE_BUILD_TESTS=OFF
cmake --build build
```

## Run

```sh
./build/pointerverse lab examples/minimal_reality.pv
./build/pointerverse repl
./build/pointerverse trace replay trace.jsonl
./build/pointerverse trace verify trace.jsonl --expect-hash 0x9f12a77103beaa20
```

## DSL sample

```txt
world new seed
object A : Node
object B : Node
link A -> B : causes weight=0.7
morphism Stabilize : Node -> Node
compose Stabilize after Stabilize
law add reject_dangling_pointer
law add bounded_weight
evolve 4
inspect graph
trace export trace.jsonl
```

## License

Pointerverse is licensed under the Apache License, Version 2.0.
