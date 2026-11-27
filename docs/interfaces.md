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
  The caller supplies a nonempty, sorted, unique sample using the key rules above.
  Training counts every (prefix, next-character) pair. A 64-bit polynomial hash
  starts at zero and updates as `hash = hash * 131 + character`; the preceding
  prefix selects row `hash % 1024`. This is our rolling-hash choice, not the
  authors' position/previous-character mapping. The 1024-by-128 table retains
  only `cdf` and `prob` doubles per cell (2 MiB on our platform); integer counters
  are temporary. CDF is exclusive of the current character. Unobserved rows stay
  zero. Each training call replaces old statistics; invalid input throws
  `std::invalid_argument` and preserves the previous table. No sampling,
  distinguishing-prefix clipping, attenuation, or terminator counts are added.
  `cell(row, character)` offers bounds-checked, read-only inspection.
- `Hpt::cdf`: produce the model coordinate after a specified prefix.
- `LocalLinearModel::fit`: derive endpoint normalization from CDF bounds.
- `LocalLinearModel::predict`: map a CDF to a slot in `[0, slot_count)`.
  Core node code separately handles prefix mismatches and reserved boundary slots.
- `gpkl`: compute group hardness for a sorted unique key range.
  Empty and singleton groups return zero. Keys must be nonempty with bytes
  1..127; unsupported keys, duplicates, and unsorted input throw
  `std::invalid_argument`. A conceptual terminator counts as a distinguishing
  byte when one key is a prefix of another; no byte beyond a view is read.
- `local_gpkl`: take the unweighted average of consecutive group scores, including
  a final short group. Singleton groups contribute zero. Empty input returns
  zero, but a zero group size always throws `std::invalid_argument`.
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

- HPT terminator handling for keys that are prefixes of other keys.
- Prediction behavior for unseen HPT contexts and the sampling policy at bulkload.
- Equal CDF endpoints and non-finite prediction values: fall back safely.
- PMSS table format, calibration hardware, and interpolation at/outside bounds.
- Index/leaf storage, copy/move rules, node layout, and subtrie ownership.

Remaining method stubs throw `std::logic_error`. Replace a stub only when its
input validation, behavior, and meaningful correctness checks are implemented.
