# Correctness testing plan

`tests/model/gpkl_test.cpp` covers prefix comparisons, hand-computed GPKL values,
local group boundaries (including a short tail), conceptual terminators, common
prefix invariance, and input validation. `tests/core/test_index.cpp` covers core
ownership, moves, input validation, and empty/single-record operations;
`tests/core/test_routing.cpp` covers prefix confirmation and slot bounds.
`tests/model/hpt_training_test.cpp`
checks hand-calculated counts and exclusive CDFs, normalization, hash collisions,
unseen rows, invalid inputs, retraining, and long strings. All four suites are
registered with CTest:

```sh
ctest --test-dir build --output-on-failure
```

Register further tests as each component is implemented. Remaining coverage:

- HPT prediction: monotonic string CDF, unseen contexts, prefix skipping, bounds.
- Local model: equal endpoints, slot boundaries, non-finite input policy.
- PMSS: interpolation grid points, upper boundaries, out-of-range inputs, mixes.
- Index/leaves: differential checks against `std::map`, scans and promotion.

Passing the current build workflow does not establish full-index correctness.
