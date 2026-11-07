# Correctness testing plan

No algorithm tests exist yet because the algorithms are placeholders. Register
real tests with CTest as each component is implemented. Suggested coverage:

- GPKL: hand-computed sorted groups, singleton/empty policy, prefix-related keys.
- HPT: normalized rows, unseen contexts, retraining, monotonic CDF, bounds.
- Local model: equal endpoints, slot boundaries, non-finite input policy.
- PMSS: interpolation grid points, upper boundaries, out-of-range inputs, mixes.
- Index/leaves: differential checks against `std::map`, scans and promotion.

Passing the current build workflow does not establish algorithm correctness.
