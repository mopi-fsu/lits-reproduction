# Research references

- Yang and Chen, *LITS: An Optimized Learned Index for Strings*, PVLDB 17(11),
  3415–3427, 2024. DOI: https://doi.org/10.14778/3681954.3682010
- Paper: https://www.vldb.org/pvldb/vol17/p3415-chen.pdf
- Original source: https://github.com/schencoding/lits/tree/master/LITS
- Reviewed upstream commit: `ce211d7c0aa15f833eaba5e342951ed49c3b81ba`.

## Source observations to verify during reproduction

- Original HPT uses position and preceding-character buckets, distinguishing
  prefixes, and attenuated character weights. Its default bulkload trains on
  all supplied keys.
- Local model parameters normalize the first/last key CDFs.
- Original PMSS embeds latency tables and interpolates in log2(key count) and GPKL.
  New measurements are needed for meaningful decisions on the target hardware.
- Review zero-prefix prediction and interpolation at maximum table coordinates
  carefully; do not carry unchecked boundary behavior into the reproduction.

Keep paper descriptions, upstream implementation details, and our own design
choices distinct in the report. Check upstream licensing and attribution before
copying implementation code or adding baseline dependencies. No upstream code
has been copied into this skeleton; no project license is selected yet.
