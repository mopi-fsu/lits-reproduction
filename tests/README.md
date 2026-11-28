# Correctness testing plan

`tests/model/gpkl_test.cpp` covers prefix comparisons, hand-computed GPKL values,
local group boundaries (including a short tail), conceptual terminators, common
prefix invariance, and input validation. `tests/core/test_index.cpp` covers core
ownership, moves, input validation, and small-index operations;
`tests/core/test_routing.cpp` covers prefix confirmation and slot bounds;
`tests/core/test_index_diff.cpp` compares the index against a `std::map` reference
(used only as an oracle) across bulkload sizes, promotion, prefix-related keys, the
subtrie fallback, update/erase, and bounded scans.
`tests/model/hpt_training_test.cpp`
checks hand-calculated counts and exclusive CDFs, normalization, hash collisions,
unseen rows, invalid inputs, retraining, and long strings.
`tests/model/hpt_cdf_test.cpp` checks Algorithm 1 with hand-calculated coordinates,
skipped-prefix contexts, collisions, monotonicity, long keys, validation, and
trained/moved-from state. `tests/model/local_model_test.cpp` checks endpoint
normalization, clamping, slot boundaries, degenerate/non-finite cases, and HPT-to-slot
composition.
`tests/leaf/compact_leaf_test.cpp` covers compact-leaf insert results (inserted,
duplicate, full), lookups, scan bounds and limits, key validation, prefix-related
keys, key ownership, and a seeded differential check against `std::map`. All
suites are registered with CTest:

```sh
ctest --test-dir build --output-on-failure
```

Register further tests as each component is implemented. Remaining coverage:

- PMSS: interpolation grid points, upper boundaries, out-of-range inputs, mixes.
- Index: differential checks against `std::map`, scans across nodes, and promotion.

Passing the current build workflow does not establish full-index correctness.
