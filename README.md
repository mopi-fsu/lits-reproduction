# LITS reproduction — COP5725

An independent course implementation and experimental reproduction of
**LITS: An Optimized Learned Index for Strings** (Yang and Chen, PVLDB 2024).

**Status: model utilities, HPT training, initial core, and compact leaf implemented.**
Prefix length, GPKL, local GPKL, HPT counting/normalization, core ownership/validation,
routing helpers, empty/single-record index operations, and compact-leaf insert,
lookup, and scan have correctness tests. The compact leaf uses simple sorted storage,
not yet the paper's h-pointer layout. Multi-record index operations, HPT string-CDF
prediction, local prediction, and PMSS remain placeholders that throw
`std::logic_error`.
There are no performance results yet. The benchmark entry point exits with an
explicit not-implemented message.

## Team and ownership

| Lead | Module | Main paths |
| --- | --- | --- |
| Md Mehrab Hossain Opi ([@mopi-fsu](https://github.com/mopi-fsu)) | HPT, CDF prediction, local models, GPKL, PMSS | `include/lits/model/`, `src/model/` |
| Bach Nguyen ([@bach6104](https://github.com/bach6104)) | Core nodes, collisions, subtree integration, index operations | `include/lits/core/`, `src/core/` |
| Taskia Taher Oyshi ([@taskia15](https://github.com/taskia15)) | Compact leaves, scans, datasets, baselines, evaluation | `include/lits/leaf/`, `src/leaf/`, `benchmarks/`, `scripts/` |

All members share integration, correctness validation, and the final report.

## Start here

Accept your collaborator invitation, clone this repository, and read your starter
issue for exact files, deliverables, dependencies, and completion checks:

- [Bach: core node ownership and bulkload validation (#1)](https://github.com/mopi-fsu/lits-reproduction/issues/1)
- [Oyshi: compact-leaf insert, lookup, and ordered scan (#2)](https://github.com/mopi-fsu/lits-reproduction/issues/2)
- [Opi: common-prefix utilities and GPKL (#3)](https://github.com/mopi-fsu/lits-reproduction/issues/3)

Read [the interface contract](docs/interfaces.md) and
[contribution workflow](CONTRIBUTING.md), create a feature branch, and open a pull
request referencing your issue. Changes to shared interfaces need agreement
from the affected module leads.

## Build

Requirements: a C++17 compiler, CMake 3.16+, and a build tool such as Make.
On Ubuntu/Debian: `sudo apt install build-essential cmake`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 2
./build/lits_example
ctest --test-dir build --output-on-failure
```

Use a separate `build-release` directory and `-DCMAKE_BUILD_TYPE=Release` for
future benchmarks. GitHub Actions builds both configurations, runs the GPKL,
HPT training, core, and routing tests, and runs the scaffold executable.
Multi-record index integration is not implemented or covered yet.

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
