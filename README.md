# LITS reproduction — COP5725

An independent course implementation and experimental reproduction of
**LITS: An Optimized Learned Index for Strings** (Yang and Chen, PVLDB 2024).

**Status: boilerplate only.** The project compiles, but index and model operations
are placeholders that throw `std::logic_error`. There are no performance results
or algorithm correctness claims yet. The benchmark entry point exits with an
explicit not-implemented message.

## Team and ownership

| Lead | Module | Main paths |
| --- | --- | --- |
| Md Mehrab Hossain Opi | HPT, CDF prediction, local models, GPKL, PMSS | `include/lits/model/`, `src/model/` |
| Bach Nguyen | Core nodes, collisions, subtree integration, index operations | `include/lits/core/`, `src/core/` |
| Taskia Taher Oyshi | Compact leaves, scans, datasets, baselines, evaluation | `include/lits/leaf/`, `src/leaf/`, `benchmarks/`, `scripts/` |

All members share integration, correctness validation, and the final report.

## Build

Requirements: a C++17 compiler, CMake 3.16+, and a build tool such as Make.
On Ubuntu/Debian: `sudo apt install build-essential cmake`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 2
./build/lits_example
```

Use a separate `build-release` directory and `-DCMAKE_BUILD_TYPE=Release` for
future benchmarks. GitHub Actions builds both configurations. The current CI
checks compilation and the scaffold executable; it is not a correctness suite.

## Layout

```text
include/lits/       Shared types and module contracts
src/model/         Opi's implementation
src/core/          Bach's implementation
src/leaf/          Oyshi's implementation
examples/          Small usage examples
benchmarks/        Workload runner and future baseline adapters
tests/             Correctness-test plan; add tests as algorithms land
scripts/           Future dataset preparation and plotting tools
data/              Local raw and processed data (ignored by Git)
docs/              Interfaces, milestones, and research references
.github/           CI and pull request template
```

Read [the interface contract](docs/interfaces.md), [milestones](docs/milestones.md),
and [contribution workflow](CONTRIBUTING.md) before starting implementation.

## Research sources

- [Paper](https://www.vldb.org/pvldb/vol17/p3415-chen.pdf)
- [Authors' original implementation](https://github.com/schencoding/lits/tree/master/LITS)

This repository starts with newly written scaffolding. Upstream source and the
paper are not vendored. See [research notes](docs/references.md) before importing
third-party code. LITS+ is outside the initial reproduction scope.
