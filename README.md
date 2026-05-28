# Pointerverse

Pointerverse is a categorical artificial reality kernel in C++.

Objects are connected by semantic pointers, transformed by morphisms, checked by
laws, observed through queries, and audited through snapshot traces.

## Build

Pointerverse uses C++20, CMake, Ninja, and vcpkg manifest mode.

```sh
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset default
cmake --build --preset default
ctest --preset default
```

On macOS, vcpkg's Catch2 port may require `pkg-config` to be available before
the test feature can be installed.

To build only the kernel and CLI:

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
  -DPOINTERVERSE_BUILD_TESTS=OFF
cmake --build build
```

## Run

```sh
./build/pointerverse lab examples/basic.pv
./build/pointerverse repl
```

## DSL sample

```txt
object A : StateNode dim=2
object B : StateNode dim=2
link A -> B : correlates_with weight=0.8 causality=causal
state A = [0.70710678+0i, 0.70710678+0i]
morph Stabilize : StateNode -> StateNode effect=stabilize
morph Readout : StateNode -> Observation effect=measure
compose Readout after Stabilize
law normalization tolerance=1e-9
law causality
evolve 8
observe A probabilities
analyze
```

## Pressure and regions

```txt
seed contradiction count=32
law bounded_pressure tolerance=0.25
evolve 5
inspect world
analyze regions
trace origin R1
```
