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
  Algorithm 1 starts with CDF zero and prefix probability one. Hash skipped
  bytes to retain the original training context, but accumulate probabilities
  only from `prefix_length` onward (conditional on the confirmed prefix).
  A fully skipped key returns zero. Unobserved/zero-probability contexts add no
  further mass. Coordinates are bounded to [0,1] but may tie, especially for
  prefix-related keys without terminator counts; core collision handling and
  local-model degeneracy handling remain necessary. Prediction before training
  (including on a moved-from model) throws `std::logic_error`. Empty or unsupported
  keys and prefix lengths beyond the key throw `std::invalid_argument`.
- `LocalLinearModel::fit`: derive endpoint normalization from CDF bounds.
  Arguments are `(min_cdf, max_cdf)` in that order. Finite bounds must satisfy
  `0 <= min_cdf <= max_cdf <= 1`, or `std::invalid_argument` is thrown. Equal
  endpoints or intervals whose reciprocal produces non-finite parameters throw
  `std::domain_error`; the core must select another structure instead of building
  a degenerate learned node. Successful fits store slope `1/(max-min)` and
  intercept `-min*slope`.
- `LocalLinearModel::predict`: map a CDF to a slot in `[0, slot_count)`.
  Apply slope and intercept, scale by the slot count, and round down. Finite
  coordinates outside the fitted interval clamp to the first/last slot. Reject
  zero slots, non-finite input/coefficients, and nonpositive slopes with
  `std::invalid_argument`. Non-finite arithmetic throws `std::domain_error`.
  Check the floating-point upper bound before conversion to an unsigned index.
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
- The sampling policy at bulkload.
- (Settled in the core: a `domain_error` from `LocalLinearModel::fit`, non-monotone CDFs,
  or no-progress partitions fall back to the subtrie stand-in; see "Structure and building".)
- PMSS table format, calibration hardware, and interpolation at/outside bounds.
- The real HOT subtrie (the core currently uses a portable stand-in) and its ownership.
- Leaf kv-entry sharing and a native leaf update/erase (core rebuilds the leaf for now).

Remaining method stubs throw `std::logic_error`. Replace a stub only when its
input validation, behavior, and meaningful correctness checks are implemented.

## Core node model (Bach)

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

### Structure and building

`LitsIndex::bulkload` validates, then builds recursively with fixed rules (PMSS and
GPKL are not used yet because `Pmss::choose` is uncalibrated and throws):

- 0 records: `empty`; 1 record: `single_record`; 2..16 records: `compact_leaf`.
- More than 16: train one global `Hpt` on all keys (no sampling yet), then build a
  `learned_node`: `prefix` = the group's common prefix, `model` = `fit(min_cdf,
  max_cdf)` over the group, `slot_count` = the group size. Keys are partitioned by
  predicted slot and each slot is built recursively (a slot collision recurses).
- Fallback to the `hot_subtrie` stand-in when the model cannot be used for a group:
  `fit` throws `domain_error` (equal/too-close CDFs), CDFs are not non-decreasing in
  key order, or every key predicts the same slot (no progress). The fallback is a
  portable byte-wise compressed trie, **not the HOT library**; it guarantees
  termination and correctness only. Its performance must not be reported as HOT/LITS.
- One `Hpt` is trained per tree and is never retrained while learned nodes exist
  (that would change every node's routing). If an index grew from a small leaf
  without one, it is trained on the first promoted leaf's records.

### Routing (lookup/insert/update/erase on a `learned_node`)

1. Confirm the node's common prefix with `relate_to_prefix` (`core/routing.hpp`):
   `within` lets the model run; `below`/`above` mean the key lies outside the node's
   key range and the model is not consulted (lookup: miss; insert: see below).
2. Ask `Hpt::cdf(key, prefix.size())` for the model coordinate (prefix already skipped).
3. `model.predict(cdf, slots.size())` gives a slot in `[0, slots.size())`.
4. Bound the slot with `bound_slot`: clamp to `slot_count - 1`; never index out of range.
5. Dispatch on the slot kind: `empty` -> miss (insert places a `single_record`);
   `single_record` -> compare keys (insert of a different key makes a two-key
   `compact_leaf`); `compact_leaf` -> leaf operation (insert reports `full` -> promote:
   drain with `scan`, add the key, rebuild the group); `learned_node` / `hot_subtrie`
   -> descend. A subtrie routes by the byte at `prefix.size()` and keeps the key equal
   to its prefix as a terminal record.

### Operations

| Operation | Behavior |
| --- | --- |
| `bulkload` | All sizes. Invalid input throws and changes nothing; empty clears; repeated bulkload replaces. |
| `lookup` / `update` | Descend by routing; leaf update rebuilds the leaf (<= 16 records) via `scan` + `insert`, since `CompactLeaf` has no update yet. |
| `insert` | Empty/single/leaf/learned/subtrie; promotes a full leaf; a key outside a learned node's prefix rebuilds that subtree (O(subtree)); splits a subtrie on a shorter shared prefix. |
| `erase` | Removes the record; a leaf shrinks (to a single record at 1 key). An internal node whose record count falls to `compact_leaf_capacity` or less is collapsed into a leaf/single record/empty, recursively up the path. |
| resizing | Each learned node and subtrie keeps `count` and `built_count`. When an insert makes `count >= 2 * built_count` the node is rebuilt from its records (new slot count and model); doubling keeps this amortized O(1) per insert. The global `Hpt` is not retrained. |
| `scan` | In-order traversal from the inclusive lower bound; only the predicted slot of each learned node needs the bound. Relies on slot order matching key order, which bulkload verifies. |

### Known limitations (follow-up work)

- Resizing is coarse: nodes rebuild only on doubling (or collapse at <= 16 records),
  with no partial slot splitting. A key outside a learned node's prefix triggers a full
  subtree rebuild; reserved boundary slots would avoid that. The HPT is trained once
  per tree, so a tree that grows far beyond its training data routes less evenly
  (still correct); retraining would need a coordinated full rebuild.
- Order preservation for keys inserted after training relies on the HPT CDF being
  monotone in key order; bulkload checks it for the trained keys, and the
  differential tests (`tests/core/test_index_diff.cpp`) exercise it, but it is not
  proven for arbitrary later inserts.
- Leaf update/erase rebuild the leaf; a native leaf API (and the kv-entry/h-pointer
  design in the leaf plan) would remove that.
- Recursion depth follows tree depth; adversarial keys such as `a`, `aa`, `aaa`, ...
  of unbounded length produce a deep subtrie chain.
- Single-threaded only; no concurrency yet. Scan materializes records.
