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

## Core node model (Bach) — proposed, pending review by Opi and Oyshi

### Slot kinds

A `Slot` (`include/lits/core/node.hpp`) is one tree position and holds exactly one
`NodeKind`: `empty`, `single_record`, `learned_node`, `compact_leaf`, `hot_subtrie`.
A `LearnedNode` has a confirmed common `prefix`, a `LocalLinearModel`, and a
`vector<Slot>` of children whose size is the model's `slot_count`.
`HotSubtrie` is an empty placeholder until subtrie ownership is decided.

### Ownership, cleanup, copy/move

- The index owns every key. `Slot::single` copies the key bytes; nothing keeps a
  `string_view` into caller storage after a call returns (matches the rule above).
- A slot owns its payload exclusively; destroying or `reset()`ing it frees the
  whole subtree. Ownership is by `unique_ptr`, so there is no manual cleanup.
- `Slot` and `LitsIndex` are **move-only**. A moved-from slot/index is empty and
  reusable. Moves are `noexcept`. Copying is deleted rather than silently deep-copying.
- Scan results are copies (`Record` owns its string), never references into nodes.

### Bulkload input

- Valid: every key supported (nonempty, bytes 1..127) and keys strictly ascending in
  byte order (this rejects both unsorted input and duplicates).
- Invalid input throws `std::invalid_argument` naming the record index, and leaves
  the index unchanged (validation runs before any mutation).
- **Empty input** is valid and clears the index (`root_kind() == empty`).
- **Repeated bulkload replaces** the previous contents; it never merges.
- Prefix-related keys (`"a"`, `"ab"`) are valid input. How they are routed is the
  open terminator decision above.
- Other operations (`lookup`, `insert`, `update`, `erase`) reject unsupported keys
  with `std::invalid_argument` instead of treating them as a miss. `scan` accepts an
  empty lower bound (start of index) and otherwise requires a supported key.

### Duplicate vs. full leaf, and promotion (needs Oyshi's agreement)

`CompactLeaf::insert` currently returns `false` for both a duplicate and a full leaf.
Until the leaf API changes, the core distinguishes them with a `lookup()` first: a hit
is a duplicate (`LitsIndex::insert` returns false); a miss on a failed insert means the
leaf is full. **Proposal:** replace the `bool` with
`enum class LeafInsert { inserted, duplicate, full }`, so the core does one call and
promotes only on `full`. Promotion: drain the leaf (`scan("", capacity)`) plus the new
record, then replace the slot with a `learned_node` (or `hot_subtrie`, per PMSS) built
from those sorted records. The leaf header is Oyshi's, so it is not changed here.

### Routing (lookup/insert/update/erase on a `learned_node`)

1. Confirm the node's common prefix with `relate_to_prefix` (`core/routing.hpp`,
   implemented and tested): `within` lets the model run; `below`/`above` mean the key
   lies outside the node's key range (handling is decided by the parent / reserved
   slots), and the model is not consulted.
2. Ask `Hpt::cdf(key, prefix.size())` for the model coordinate (prefix already skipped).
3. `model.predict(cdf, slots.size())` gives a slot in `[0, slots.size())`. Non-finite or
   degenerate-CDF cases fall back as defined by Opi's decision above.
4. Bound the slot defensively with `bound_slot` (implemented and tested): clamp to
   `slot_count - 1`; never index out of range.
5. Dispatch on the slot kind: `empty` → miss (insert places a `single_record`);
   `single_record` → compare keys; `compact_leaf` → leaf operation;
   `learned_node` / `hot_subtrie` → descend with the longer confirmed prefix.
   A collision (insert into an occupied slot with a different key) promotes that slot
   into a child structure.

### Working vs. stubbed today

| Operation | Empty | Single record | Learned / leaf / HOT root |
| --- | --- | --- | --- |
| `bulkload` | works (0 records clears) | works (1 record) | > 1 record throws `logic_error` |
| `lookup` / `update` / `erase` | works | works | `logic_error` |
| `insert` | works (becomes single) | duplicate → false; new key throws `logic_error` (needs promotion) | `logic_error` |
| `scan` | works | works | `logic_error` |

All validation is implemented. Unavailable paths throw rather than emulate the index
with a temporary map.
