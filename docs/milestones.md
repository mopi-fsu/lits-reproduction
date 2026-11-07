# Implementation milestones

## 1. Contracts and development setup — all members

- Build the skeleton and review shared interfaces.
- Agree on key ordering, ownership, edge cases, and promotion between node types.
- Create issues for the three modules and assign leads.

## 2. Independent modules

- Opi: common-prefix utilities and GPKL with hand-calculated cases; HPT counts
  and normalized rows; recursive CDF and local prediction; PMSS after calibration.
- Bach: node representation, ownership, sorted bulkload, collision handling,
  lookup, insert, update, delete, and subtree fallback.
- Oyshi: compact leaf operations and scans; dataset loading and deterministic
  generators; baseline adapters and benchmark command-line design.

## 3. First integrated single-threaded index

- Compare index operations and scans against a `std::map` reference.
- Cover misses, duplicates, prefix-related keys, boundaries, and rebuilds.
- Use sanitizers before optimizing memory layouts.

## 4. Evaluation

- Calibrate PMSS on the target hardware using working LIT and HOT implementations.
- Run reproducible point/scan workloads and report memory and bulkload cost.
- Add ablations for HPT, compact leaves, and subtree selection.
- Add concurrency only after single-threaded correctness is established.

The current repository completes the skeleton portion of milestone 1 only.
