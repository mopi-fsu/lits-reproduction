# Initial interface contract

These headers are starting contracts, not the authors' exact APIs. Agree on
changes through a PR involving the affected module leads.

## Keys, values, and ownership

- Initial keys: nonempty strings containing only bytes 1..127; no embedded NUL.
  Reject unsupported input instead of silently truncating it. Broader byte/UTF-8
  support is a later explicit change. Ordering is lexicographic byte order.
- Values are unsigned 64-bit integers. Zero is valid; use `std::optional` for misses.
- Bulkload input must be sorted and unique. The core owns copies after bulkload
  and insertion. Returned scan records own their strings.
- `std::string_view` arguments are borrowed only for the duration of a call.
  Model training must not retain views into caller storage.
- Prefix lengths count bytes from the beginning of the original key. Prediction
  skips only the prefix already confirmed by the core.

## Model to core (Opi → Bach)

- `Hpt::train`: construct a global model before building learned nodes.
- `Hpt::cdf`: produce the model coordinate after a specified prefix.
- `LocalLinearModel::fit`: derive endpoint normalization from CDF bounds.
- `LocalLinearModel::predict`: map a CDF to a slot in `[0, slot_count)`.
  Core node code separately handles prefix mismatches and reserved boundary slots.
- `gpkl`: compute group hardness for a sorted unique key range.
- `Pmss::choose`: choose compact leaf, learned node, or HOT subtree using key
  count, hardness, and workload mix. Calibration belongs to Opi; the measurement
  runner and baseline adapters are coordinated with Oyshi.

## Leaf to core (Oyshi → Bach)

- Compact leaves hold up to `compact_leaf_capacity` (initially 16) records.
- Insert returns false for a duplicate or a full leaf; the core distinguishes
  these cases and handles promotion. Coordinate the exact promotion API before use.
- Lookup returns an optional value. Scan is inclusive of its lower bound and
  returns at most `limit` records in ascending order; zero limit returns none.
- The initial scan API materializes records for ease of integration. An iterator
  API and memory layout must be agreed on before scan performance experiments.

## Decisions to settle before implementation

- Empty/singleton GPKL and the final short local-GPKL group.
- Terminator handling for keys that are prefixes of other keys.
- Unseen HPT rows, training reset/retraining, sampling, and table ownership.
- Equal CDF endpoints and non-finite prediction values: fall back safely.
- PMSS table format, calibration hardware, and interpolation at/outside bounds.
- Index/leaf storage, copy/move rules, node layout, and subtrie ownership.

Every current method stub throws `std::logic_error`. Replace a stub only when its
input validation, behavior, and meaningful correctness checks are implemented.
