# ADR 0011 — Optional LD_PRELOAD shim to disable fault read-around

**Status:** accepted (2026-10-23). Numbered 0011 because the `ayush` docs
branch already uses 0005–0010.

**Context.** Phase 7 on OLMoE (BENCHMARKS.md) showed that the dominant cost
under memory pressure is mmap fault **read-around**, not eviction policy.
Each fault reads a 4 MiB window on this btrfs, pulling in neighbouring
experts that are never used:
- 3.7× SSD read amplification;
- replay traffic of 1.80 GB/token versus 0.16 GB/token with read-around
  off.

It also makes moepagerd's sentinels fire falsely, so completion readahead
alone *hurt* performance.

Disabling read-around needs `madvise(MADV_RANDOM)` on the engine's own
mapping:
- `process_madvise` cannot apply `MADV_RANDOM` to another process;
- the per-device `read_ahead_kb` is global and needs root;
- llama.cpp applies `MADV_RANDOM` only under `--numa` on multi-node
  machines;
- llama.cpp's `--lazy-mode on` moves weights to anonymous memory, where
  they were OOM-killed under the wall.

**Decision.** Ship `tools/preload/libmoepager_preload.so`, a 64-line
`LD_PRELOAD` shim. It wraps `mmap`, and for read-only shared mappings of
`*.gguf` files calls `madvise(MADV_RANDOM)`. The engine's source and binary
are unchanged. The shim is optional, and moepagerd warns (via
`/proc/<pid>/smaps` `VmFlags`) when read-around is still on.

**Consequences.**
- The PRD non-goal "no LD_PRELOAD" is relaxed to this one hint.
- The recommended configuration is: engine with `--no-repack` plus the
  shim, and moepagerd for expert-completion readahead (measured best:
  BENCHMARKS.md, experiment D).
- The cleaner long-term fix is an upstream llama.cpp option to apply
  `MADV_RANDOM` to expert tensors on single-node machines.
