# Team workflow

1. Open an issue describing a small deliverable and its acceptance criteria.
2. Branch from the latest `main`. Suggested names: `opi/hpt`, `bach/core`,
   `oyshi/compact-leaf`. For multiple tasks, add a specific suffix.
3. Work primarily inside your module. Discuss changes to shared headers in
   `include/lits/` with affected teammates before merging.
4. Build locally using the README commands. Add meaningful correctness tests
   when replacing stubs; register them with CTest at that point.
5. Open a pull request, describe validation, and request another teammate's review.
6. Merge after review and passing CI. Keep generated binaries and datasets out of Git.

PR review and passing CI are team conventions until repository rules are enabled;
this scaffold does not configure branch protection or review enforcement.

Keep portable C++17 code in the first implementation. Add x86-specific HOT/SIMD
optimizations behind explicit build options with documented CPU requirements.
Validate the single-threaded index before implementing concurrency.

Never fabricate benchmark results. Record hardware, compiler, flags, dataset
version, random seed, workload mix, and index configuration for measured results.
