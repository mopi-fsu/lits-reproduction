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
- Insert returns `LeafInsert::inserted`, `duplicate`, or `full`. A duplicate is
  reported even when the leaf is full. The core promotes the leaf only on `full`.
- Lookup returns an optional value. Scan is inclusive of its lower bound and
  returns at most `limit` records in ascending order; zero limit returns none.
- The initial scan API materializes records for ease of integration. An iterator
  API and memory layout must be agreed on before scan performance experiments.

### Compact leaf: current implementation

**Storage.** The first implementation keeps a sorted `std::vector<Record>` of at
most 16 records, and each record owns a copy of its key. Like the paper's default
design, the array holds exactly one slot per record: an insert builds a new array
with one more slot instead of reserving 16. It is otherwise deliberately simpler
than the paper. Keys are stored inline and found by binary search, with no
h-pointers or hash filtering, so it does not reproduce the paper's memory layout
or performance. Do not report scan or lookup timings from it as LITS results.

**Unsupported input.** `insert`, `lookup`, and `scan` throw `std::invalid_argument`
for an unsupported key instead of returning a result or a miss, so a rejected key
cannot be mistaken for a duplicate, a full leaf, or an absent key. Validation runs
first, including on a full leaf or with a zero scan limit. The one addition to the
key rules is that `scan` accepts an empty lower bound, meaning the first record.

**Insert result and promotion (agreed with Bach).** `insert` returns
`enum class LeafInsert { inserted, duplicate, full }`, as Bach proposed, so the
core needs one call and no extra `lookup`. On `duplicate`, `LitsIndex::insert`
returns false. On `full`, the core calls `scan("", compact_leaf_capacity)` to get
every record, in order and owning its key, adds the new record, and builds the
replacement node from them.

### Compact leaf: plan for the paper's design

**What the paper describes** (Sections 3.1, 3.3, 4.1, and 4.4):

- A compact leaf is a header plus an array of up to w = 16 h-pointers, sorted in
  key order. The paper measured limits of 8, 16, and 32 and chose 16.
- An h-pointer is 64 bits: a 16-bit hash of the key and a 48-bit pointer to a
  kv-entry. The key and value live in the kv-entry, not in the leaf.
- Search compares the search key's hash with every h-pointer in order and
  dereferences a pointer only on a hash match, then compares the full key. The
  paper reports a 0.0015% false-positive rate and prefers this to binary search.
- A leaf with k keys uses exactly k slots, and an insert creates a node with one
  more slot. Preallocating 16 slots cost up to 93% extra space for no significant
  speedup. A SIMD search variant gave little benefit.
- A colliding key that meets a single kv-entry creates a two-key compact leaf.
  Bulkload builds a compact leaf for any range of at most 16 keys. An insert into
  a full leaf triggers a PMSS decision that replaces it with a model-based node
  or a subtrie (core and model modules).
- Update changes the value in the kv-entry or repoints the h-pointer. Delete
  removes the key from the leaf. Scans use an iterator with `next()`, backed by a
  stack of nodes from the root, and rely on leaves being sorted.
- The paper text does not name the 16-bit hash function. Check the upstream code
  before choosing one.

**Planned steps, in order.** Each needs agreement as noted; none is implemented.

1. *Kv-entry ownership (with Bach).* Model-based node items also point to
   kv-entries, so one kv-entry type should be shared by the core and the leaves.
   Proposal: the index owns kv-entries, and leaves hold pointers to them. Then
   promoting a full leaf moves pointers without copying keys, and returned scan
   records still own copies of their keys.
2. *Portable h-pointers first.* Store `{16-bit hash, pointer}` as a two-field
   struct. Pack them into one 64-bit word only behind an explicit build option,
   since that assumes pointers fit in 48 bits. Confirm this on the benchmark
   server, and on any other target, before enabling it.
3. *Hash-filtered search.* Compute the key's hash once, scan the h-pointers in
   order, and compare full keys only on a hash match. Add a test that forces hash
   collisions to check that false positives are handled correctly.
4. *Operations the core will need (with Bach).* Building a leaf from at most 16
   sorted unique records (for bulkload and two-key collisions), update, delete,
   handing a full leaf's records over for promotion, and a scan iterator to
   replace the materialized `scan` result.
5. *Configurable capacity (with all leads).* The 8/16/32 ablation needs the leaf
   limit to be a parameter. `compact_leaf_capacity` is in the shared
   `include/lits/types.hpp`, so changing it needs agreement.
6. *Measure only after steps 2 and 3 land.* Record the hardware, compiler, flags,
   dataset, and seed with every result, as `CONTRIBUTING.md` requires.

Keep the behavior and tests of the current leaf when the layout changes.

## Decisions to settle before implementation

- HPT terminator handling for keys that are prefixes of other keys.
- Prediction behavior for unseen HPT contexts and the sampling policy at bulkload.
- Equal CDF endpoints and non-finite prediction values: fall back safely.
- PMSS table format, calibration hardware, and interpolation at/outside bounds.
- Index/leaf storage, copy/move rules, node layout, and subtrie ownership.

Remaining method stubs throw `std::logic_error`. Replace a stub only when its
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

### Duplicate vs. full leaf, and promotion (agreed by Oyshi)

`CompactLeaf::insert` returns `enum class LeafInsert { inserted, duplicate, full }`
(implemented in the leaf, see "Compact leaf: current implementation" above), so the
core does one call: `duplicate` means `LitsIndex::insert` returns false, and the core
promotes only on `full`. A duplicate is reported even when the leaf is full.
Promotion: drain the leaf (`scan("", capacity)`) plus the new record, then replace
the slot with a `learned_node` (or `hot_subtrie`, per PMSS) built from those sorted
records.

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
